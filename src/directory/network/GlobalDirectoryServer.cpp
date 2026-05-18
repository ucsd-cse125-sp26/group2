/// @file GlobalDirectoryServer.cpp
/// @brief UDP-only central directory, NAT assist, and relay service.

#include "GlobalDirectoryServer.hpp"

#include "network/transport/PacketHeader.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <limits>
#include <random>

namespace
{
std::string addressString(NET_Address* addr)
{
    const char* raw = addr ? NET_GetAddressString(addr) : nullptr;
    return raw ? raw : "";
}

bool sameEndpoint(const net::UdpEndpointAddr& a, const net::UdpEndpointAddr& b)
{
    return a.port == b.port && addressString(a.addr) == addressString(b.addr);
}

std::uint64_t randomRelayToken()
{
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist{1, std::numeric_limits<std::uint64_t>::max()};
    return dist(rng);
}
} // namespace

bool GlobalDirectoryServer::init(const char* bindHost, Uint16 tcpPort, Uint16 udpPort)
{
    (void)tcpPort;
    if (!udpEndpoint_.open(bindHost, udpPort)) {
        SDL_Log("[directory] failed to create UDP directory/relay listener on %u", udpPort);
        return false;
    }

    SDL_Log("[directory] UDP directory/relay listening on %u", udpPort);
    shouldStop_.store(false, std::memory_order_relaxed);
    return true;
}

void GlobalDirectoryServer::stop()
{
    shouldStop_.store(true, std::memory_order_relaxed);
    clientsByNonce_.clear();
    servers_.clear();
    udpEndpoint_.close();
}

void GlobalDirectoryServer::run()
{
    while (!shouldStop_.load(std::memory_order_relaxed)) {
        pollUdp();
        pruneExpired();
        SDL_Delay(2);
    }
}

void GlobalDirectoryServer::pollUdp()
{
    net::UdpReceivedMessage msg;
    int drained = 0;
    constexpr int k_maxDatagramsPerCycle = 1024;
    while (drained < k_maxDatagramsPerCycle && udpEndpoint_.tryReceive(msg)) {
        ++drained;
        const auto kind = static_cast<net::PacketKind>(msg.header.kind);
        if (kind == net::PacketKind::DirectoryControl) {
            handleDirectoryControl(msg);
        } else if (kind == net::PacketKind::RelayPayload) {
            handleRelayPayload(msg);
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

    for (auto it = clientsByNonce_.begin(); it != clientsByNonce_.end();) {
        if (now - it->second.lastSeenMs > 10000) {
            it = clientsByNonce_.erase(it);
        } else {
            ++it;
        }
    }
}

void GlobalDirectoryServer::handleDirectoryControl(const net::UdpReceivedMessage& msg)
{
    net::discovery::DirectoryMessage kind{};
    const std::uint8_t* payload = nullptr;
    std::size_t payloadLen = 0;
    if (!net::discovery::parseEnvelope(msg.payload.data(), msg.payload.size(), kind, payload, payloadLen))
        return;

    switch (kind) {
    case net::discovery::DirectoryMessage::RegisterServer:
    case net::discovery::DirectoryMessage::Heartbeat:
        handleRegistration(msg, kind, payload, payloadLen);
        break;
    case net::discovery::DirectoryMessage::ListRequest:
        handleListRequest(msg);
        break;
    case net::discovery::DirectoryMessage::PunchRequest:
        handlePunchRequest(msg, payload, payloadLen);
        break;
    default:
        break;
    }
}

void GlobalDirectoryServer::handleRegistration(const net::UdpReceivedMessage& msg,
                                               net::discovery::DirectoryMessage kind,
                                               const std::uint8_t* data,
                                               std::size_t len)
{
    const auto maybeReg = net::discovery::decodeRegistration(data, len);
    if (!maybeReg)
        return;

    const auto& reg = *maybeReg;
    const std::uint32_t id = reg.serverId != 0 ? reg.serverId : nextServerId_++;
    ServerRecord& record = servers_[id];
    const std::string host = addressString(msg.from.addr);

    record.info.id = id;
    record.info.name = reg.name.empty() ? "Unnamed Server" : reg.name;
    record.info.host = host;
    record.info.gamePort = reg.gamePort;
    record.info.udpHost = host;
    record.info.udpPort = msg.from.port;
    record.info.currentPlayers = reg.currentPlayers;
    record.info.maxPlayers = reg.maxPlayers;
    record.info.natTraversalReady = true;
    record.lastSeenMs = SDL_GetTicks();
    record.endpoint = msg.from;

    if (kind == net::discovery::DirectoryMessage::RegisterServer) {
        SDL_Log("[directory] registered server %u '%s' at %s:%u",
                id,
                record.info.name.c_str(),
                record.info.udpHost.c_str(),
                record.info.udpPort);
    }

    sendDirectory(msg.from,
                  net::discovery::encodeRegisterAck(
                      {.accepted = true, .serverId = id, .publicHost = host, .message = "registered"}));
}

void GlobalDirectoryServer::handleListRequest(const net::UdpReceivedMessage& msg)
{
    const Uint64 now = SDL_GetTicks();
    std::vector<net::discovery::ServerInfo> active;
    active.reserve(servers_.size());
    for (const auto& [_, record] : servers_) {
        net::discovery::ServerInfo info = record.info;
        info.lastSeenMs = now - record.lastSeenMs;
        active.push_back(std::move(info));
    }
    sendDirectory(msg.from, net::discovery::encodeServerList(active));
}

void GlobalDirectoryServer::handlePunchRequest(const net::UdpReceivedMessage& msg,
                                               const std::uint8_t* data,
                                               std::size_t len)
{
    const auto maybeReq = net::discovery::decodePunchRequest(data, len);
    if (!maybeReq)
        return;

    ClientEndpoint& client = clientsByNonce_[maybeReq->clientNonce];
    client.endpoint = msg.from;
    client.relayToken = randomRelayToken();
    client.lastSeenMs = SDL_GetTicks();

    net::discovery::PunchResponse resp;
    resp.message = "server not found";
    if (auto it = servers_.find(maybeReq->serverId); it != servers_.end()) {
        resp.accepted = true;
        resp.server = it->second.info;
        resp.relayToken = client.relayToken;
        resp.message = "ok";

        const net::discovery::UdpPunchPeer toServer{
            .clientNonce = maybeReq->clientNonce,
            .host = addressString(msg.from.addr),
            .port = msg.from.port,
        };
        sendDirectory(it->second.endpoint,
                      net::discovery::makeEnvelope(net::discovery::DirectoryMessage::PunchPeer,
                                                   net::discovery::encodeUdpPunchPeer(toServer)));
    }

    sendDirectory(msg.from, net::discovery::encodePunchResponse(resp));
}

void GlobalDirectoryServer::handleRelayPayload(const net::UdpReceivedMessage& msg)
{
    constexpr std::size_t k_relayEnvelopeBytes =
        sizeof(std::uint32_t) * 2 + sizeof(std::uint64_t) + sizeof(std::uint16_t);
    if (msg.payload.size() < k_relayEnvelopeBytes)
        return;

    const std::uint32_t serverId = net::readU32Le(msg.payload.data());
    const std::uint32_t clientNonce = net::readU32Le(msg.payload.data() + 4);
    const std::uint64_t relayToken = net::readU64Le(msg.payload.data() + 8);
    auto serverIt = servers_.find(serverId);
    if (serverIt == servers_.end())
        return;

    ServerRecord& server = serverIt->second;
    const bool fromServer = sameEndpoint(msg.from, server.endpoint);
    if (fromServer) {
        auto clientIt = clientsByNonce_.find(clientNonce);
        if (clientIt == clientsByNonce_.end())
            return;
        sendRelay(clientIt->second.endpoint, msg.payload.data(), msg.payload.size());
    } else {
        auto clientIt = clientsByNonce_.find(clientNonce);
        if (clientIt == clientsByNonce_.end() || relayToken == 0 || relayToken != clientIt->second.relayToken)
            return;
        clientIt->second.endpoint = msg.from;
        clientIt->second.lastSeenMs = SDL_GetTicks();
        sendRelay(server.endpoint, msg.payload.data(), msg.payload.size());
    }
}

void GlobalDirectoryServer::sendDirectory(const net::UdpEndpointAddr& dest, const std::vector<std::uint8_t>& payload)
{
    if (!dest.addr || payload.empty())
        return;
    net::PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(net::PacketKind::DirectoryControl);
    hdr.channel = static_cast<std::uint8_t>(net::ChannelId::ControlReliableOrdered);
    udpEndpoint_.send(dest, hdr, payload.data(), static_cast<int>(payload.size()));
}

void GlobalDirectoryServer::sendRelay(const net::UdpEndpointAddr& dest, const std::uint8_t* payload, std::size_t len)
{
    if (!dest.addr || !payload || len == 0)
        return;
    net::PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(net::PacketKind::RelayPayload);
    hdr.channel = static_cast<std::uint8_t>(net::ChannelId::ControlReliableOrdered);
    udpEndpoint_.sendFragmented(dest, hdr, payload, static_cast<int>(len), 1);
}
