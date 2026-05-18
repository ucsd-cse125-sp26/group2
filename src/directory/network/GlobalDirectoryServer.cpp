/// @file GlobalDirectoryServer.cpp
/// @brief Central directory server for global server browser and NAT assist.

#include "GlobalDirectoryServer.hpp"

#include "network/transport/PacketHeader.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace
{
std::string addressString(NET_Address* addr)
{
    const char* raw = addr ? NET_GetAddressString(addr) : nullptr;
    return raw ? raw : "";
}

std::string remoteHost(NET_StreamSocket* socket)
{
    NET_Address* addr = NET_GetStreamSocketAddress(socket);
    std::string host = addressString(addr);
    if (addr)
        NET_UnrefAddress(addr);
    return host;
}
} // namespace

bool GlobalDirectoryServer::init(const char* bindHost, Uint16 tcpPort, Uint16 udpPort)
{
    NET_Address* addr = nullptr;
    if (bindHost != nullptr && bindHost[0] != '\0') {
        addr = NET_ResolveHostname(bindHost);
        if (!addr || NET_WaitUntilResolved(addr, -1) != NET_SUCCESS) {
            SDL_Log("[directory] failed to resolve bind host '%s': %s", bindHost, SDL_GetError());
            if (addr)
                NET_UnrefAddress(addr);
            return false;
        }
    }

    tcpServer_ = NET_CreateServer(addr, tcpPort);
    if (addr)
        NET_UnrefAddress(addr);
    if (!tcpServer_) {
        SDL_Log("[directory] failed to create TCP listener on %u: %s", tcpPort, SDL_GetError());
        return false;
    }

    if (!udpEndpoint_.open(bindHost, udpPort)) {
        SDL_Log("[directory] failed to create UDP listener on %u", udpPort);
        NET_DestroyServer(tcpServer_);
        tcpServer_ = nullptr;
        return false;
    }

    SDL_Log("[directory] listening on TCP %u / UDP %u", tcpPort, udpPort);
    shouldStop_.store(false, std::memory_order_relaxed);
    return true;
}

void GlobalDirectoryServer::stop()
{
    shouldStop_.store(true, std::memory_order_relaxed);
    clients_.clear();
    udpEndpoint_.close();
    if (tcpServer_) {
        NET_DestroyServer(tcpServer_);
        tcpServer_ = nullptr;
    }
}

void GlobalDirectoryServer::run()
{
    while (!shouldStop_.load(std::memory_order_relaxed)) {
        acceptClients();
        pollTcpClients();
        pollUdp();
        pruneExpired();
        SDL_Delay(10);
    }
}

void GlobalDirectoryServer::acceptClients()
{
    NET_StreamSocket* socket = nullptr;
    while (NET_AcceptClient(tcpServer_, &socket) && socket != nullptr) {
        NET_SetStreamSocketNoDelay(socket, true);
        TcpConnection conn;
        conn.stream = MessageStream(socket);
        conn.host = remoteHost(socket);
        SDL_Log("[directory] accepted TCP client from %s", conn.host.c_str());
        clients_.push_back(std::move(conn));
        socket = nullptr;
    }
}

void GlobalDirectoryServer::pollTcpClients()
{
    for (auto it = clients_.begin(); it != clients_.end();) {
        const bool ok =
            it->stream.poll([this, &conn = *it](const void* data, Uint32 len) { handleTcpMessage(conn, data, len); });
        if (!ok) {
            if (it->stream.socket)
                NET_DestroyStreamSocket(it->stream.socket);
            it = clients_.erase(it);
        } else {
            ++it;
        }
    }
}

void GlobalDirectoryServer::pollUdp()
{
    net::UdpReceivedMessage msg;
    constexpr int k_maxDatagramsPerCycle = 256;
    int drained = 0;
    while (drained < k_maxDatagramsPerCycle && udpEndpoint_.tryReceive(msg)) {
        ++drained;
        if (msg.payload.empty()) {
            msg.from.release();
            continue;
        }

        const auto maybeHello = net::discovery::decodeUdpHello(msg.payload.data(), msg.payload.size());
        const std::string host = addressString(msg.from.addr);
        const Uint64 now = SDL_GetTicks();

        if (maybeHello && maybeHello->role == net::discovery::UdpRole::Server) {
            if (auto it = servers_.find(maybeHello->idOrNonce); it != servers_.end()) {
                it->second.info.udpHost = host;
                it->second.info.udpPort = msg.from.port;
                it->second.info.natTraversalReady = true;
                it->second.lastSeenMs = now;
            }
        } else if (maybeHello && maybeHello->role == net::discovery::UdpRole::Client) {
            clientUdpByNonce_[maybeHello->idOrNonce] =
                ClientUdpEndpoint{.host = host, .port = msg.from.port, .lastSeenMs = now};
        }

        msg.from.release();
    }
}

void GlobalDirectoryServer::pruneExpired()
{
    const Uint64 now = SDL_GetTicks();
    for (auto it = servers_.begin(); it != servers_.end();) {
        if (now - it->second.lastSeenMs > net::discovery::k_serverTtlMs) {
            SDL_Log("[directory] expiring server %u (%s)", it->first, it->second.info.name.c_str());
            it = servers_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = clientUdpByNonce_.begin(); it != clientUdpByNonce_.end();) {
        if (now - it->second.lastSeenMs > 10000) {
            it = clientUdpByNonce_.erase(it);
        } else {
            ++it;
        }
    }
}

void GlobalDirectoryServer::handleTcpMessage(TcpConnection& conn, const void* data, Uint32 len)
{
    net::discovery::DirectoryMessage kind{};
    const std::uint8_t* payload = nullptr;
    std::size_t payloadLen = 0;
    if (!net::discovery::parseEnvelope(data, len, kind, payload, payloadLen))
        return;

    switch (kind) {
    case net::discovery::DirectoryMessage::RegisterServer:
    case net::discovery::DirectoryMessage::Heartbeat:
        handleRegistration(conn, kind, payload, payloadLen);
        break;
    case net::discovery::DirectoryMessage::ListRequest:
        handleListRequest(conn);
        break;
    case net::discovery::DirectoryMessage::PunchRequest:
        handlePunchRequest(conn, payload, payloadLen);
        break;
    default:
        break;
    }
}

void GlobalDirectoryServer::handleRegistration(TcpConnection& conn,
                                               net::discovery::DirectoryMessage kind,
                                               const std::uint8_t* data,
                                               std::size_t len)
{
    const auto maybeReg = net::discovery::decodeRegistration(data, len);
    if (!maybeReg)
        return;

    const net::discovery::ServerRegistration& reg = *maybeReg;
    const std::uint32_t id = reg.serverId != 0 ? reg.serverId : nextServerId_++;
    ServerRecord& record = servers_[id];
    const std::string oldUdpHost = record.info.udpHost;
    const std::uint16_t oldUdpPort = record.info.udpPort;
    const bool oldNatReady = record.info.natTraversalReady;

    record.info.id = id;
    record.info.name = reg.name.empty() ? "Unnamed Server" : reg.name;
    record.info.host = conn.host;
    record.info.gamePort = reg.gamePort;
    record.info.currentPlayers = reg.currentPlayers;
    record.info.maxPlayers = reg.maxPlayers;
    record.info.udpHost = oldUdpHost;
    record.info.udpPort = oldUdpPort;
    record.info.natTraversalReady = oldNatReady;
    record.lastSeenMs = SDL_GetTicks();

    if (kind == net::discovery::DirectoryMessage::RegisterServer) {
        SDL_Log("[directory] registered server %u '%s' at %s:%u",
                id,
                record.info.name.c_str(),
                record.info.host.c_str(),
                record.info.gamePort);
    }

    sendTcp(conn,
            net::discovery::encodeRegisterAck(
                {.accepted = true, .serverId = id, .publicHost = conn.host, .message = "registered"}));
}

void GlobalDirectoryServer::handleListRequest(TcpConnection& conn)
{
    const Uint64 now = SDL_GetTicks();
    std::vector<net::discovery::ServerInfo> active;
    active.reserve(servers_.size());
    for (const auto& [_, record] : servers_) {
        net::discovery::ServerInfo info = record.info;
        info.lastSeenMs = now - record.lastSeenMs;
        active.push_back(std::move(info));
    }
    sendTcp(conn, net::discovery::encodeServerList(active));
}

void GlobalDirectoryServer::handlePunchRequest(TcpConnection& conn, const std::uint8_t* data, std::size_t len)
{
    const auto maybeReq = net::discovery::decodePunchRequest(data, len);
    if (!maybeReq)
        return;

    net::discovery::PunchResponse resp;
    resp.message = "server not found";

    if (auto serverIt = servers_.find(maybeReq->serverId); serverIt != servers_.end()) {
        resp.accepted = true;
        resp.server = serverIt->second.info;
        resp.message = "ok";

        if (auto clientIt = clientUdpByNonce_.find(maybeReq->clientNonce);
            clientIt != clientUdpByNonce_.end() && !serverIt->second.info.udpHost.empty())
        {
            const auto& client = clientIt->second;
            const auto& server = serverIt->second.info;
            sendUdpTo(server.udpHost,
                      server.udpPort,
                      net::discovery::encodeUdpPunchPeer(
                          {.clientNonce = maybeReq->clientNonce, .host = client.host, .port = client.port}),
                      server.id);
            sendUdpTo(client.host,
                      client.port,
                      net::discovery::encodeUdpPunchPeer(
                          {.clientNonce = maybeReq->clientNonce, .host = server.udpHost, .port = server.udpPort}),
                      maybeReq->clientNonce);
            SDL_Log("[directory] punch assist server=%u client=%s:%u serverUdp=%s:%u",
                    server.id,
                    client.host.c_str(),
                    client.port,
                    server.udpHost.c_str(),
                    server.udpPort);
        }
    }

    sendTcp(conn, net::discovery::encodePunchResponse(resp));
}

void GlobalDirectoryServer::sendTcp(TcpConnection& conn, const std::vector<std::uint8_t>& payload)
{
    if (conn.stream.socket)
        conn.stream.send(payload.data(), static_cast<Uint32>(payload.size()));
}

void GlobalDirectoryServer::sendUdpTo(const std::string& host,
                                      Uint16 port,
                                      const std::vector<std::uint8_t>& payload,
                                      std::uint32_t connectionId)
{
    if (host.empty() || port == 0 || payload.empty())
        return;

    NET_Address* addr = NET_ResolveHostname(host.c_str());
    if (!addr || NET_WaitUntilResolved(addr, 1000) != NET_SUCCESS) {
        if (addr)
            NET_UnrefAddress(addr);
        return;
    }

    net::UdpEndpointAddr dest{.addr = addr, .port = port};
    net::PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(net::PacketKind::KeepAlive);
    hdr.connectionId = connectionId;
    hdr.channel = static_cast<std::uint8_t>(net::ChannelId::Unreliable);
    udpEndpoint_.send(dest, hdr, payload.data(), static_cast<int>(payload.size()));
    dest.release();
}
