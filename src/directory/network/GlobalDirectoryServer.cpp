/// @file GlobalDirectoryServer.cpp
/// @brief UDP-only central directory, NAT assist, and relay service.

#include "GlobalDirectoryServer.hpp"

#include "network/crypto/HmacSha256.hpp"
#include "network/transport/PacketHeader.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

namespace
{
constexpr std::size_t k_maxAdvertisedServers = 256;
constexpr std::size_t k_maxRelayClients = 4096;
constexpr std::size_t k_maxServersPerList = 5;
constexpr std::size_t k_maxServerNameBytes = 64;

std::string addressString(NET_Address* addr)
{
    const char* raw = addr ? NET_GetAddressString(addr) : nullptr;
    return raw ? raw : "";
}

bool sameEndpoint(const net::UdpEndpointAddr& a, const net::UdpEndpointAddr& b)
{
    return a.port == b.port && addressString(a.addr) == addressString(b.addr);
}

std::uint64_t relaySessionKey(std::uint32_t serverId, std::uint32_t clientNonce)
{
    return (static_cast<std::uint64_t>(serverId) << 32) | clientNonce;
}

std::string sanitizeServerName(const std::string& name)
{
    if (name.empty())
        return "Unnamed Server";
    return name.substr(0, k_maxServerNameBytes);
}

std::vector<std::uint8_t> loadRelaySecret()
{
    if (const char* envSecret = SDL_getenv("GROUP2_RELAY_SECRET")) {
        const std::size_t len = std::strlen(envSecret);
        if (len >= 32)
            return std::vector<std::uint8_t>(envSecret, envSecret + len);
        SDL_Log("[directory] GROUP2_RELAY_SECRET is shorter than 32 bytes; using process-random fallback");
    }

    std::vector<std::uint8_t> secret(32);
    std::random_device rd;
    for (std::uint8_t& byte : secret)
        byte = static_cast<std::uint8_t>(rd());
    SDL_Log("[directory] GROUP2_RELAY_SECRET not set; relay tokens will be valid only for this process");
    return secret;
}

void appendTokenMaterial(std::vector<std::uint8_t>& out,
                         std::uint32_t serverId,
                         std::uint32_t clientNonce,
                         Uint64 expiresAtMs)
{
    static constexpr char k_domain[] = "group2-relay-token-v1";
    out.insert(out.end(), k_domain, k_domain + sizeof(k_domain) - 1);
    const std::size_t off = out.size();
    out.resize(off + sizeof(std::uint32_t) * 2 + sizeof(std::uint64_t));
    net::writeU32Le(out.data() + off, serverId);
    net::writeU32Le(out.data() + off + 4, clientNonce);
    net::writeU64Le(out.data() + off + 8, expiresAtMs);
}
} // namespace

bool GlobalDirectoryServer::init(const char* bindHost, Uint16 tcpPort, Uint16 udpPort)
{
    (void)tcpPort;
    if (!udpEndpoint_.open(bindHost, udpPort)) {
        SDL_Log("[directory] failed to create UDP directory/relay listener on %u", udpPort);
        return false;
    }

    relaySecret_ = loadRelaySecret();
    SDL_Log("[directory] UDP directory/relay listening on %u", udpPort);
    shouldStop_.store(false, std::memory_order_relaxed);
    return true;
}

void GlobalDirectoryServer::stop()
{
    shouldStop_.store(true, std::memory_order_relaxed);
    clientsByRelaySession_.clear();
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

    for (auto it = clientsByRelaySession_.begin(); it != clientsByRelaySession_.end();) {
        if (now - it->second.lastSeenMs > 10000) {
            it = clientsByRelaySession_.erase(it);
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
    const std::string host = addressString(msg.from.addr);
    auto existing = servers_.find(id);
    if (reg.serverId != 0 && existing != servers_.end() && !sameEndpoint(msg.from, existing->second.endpoint)) {
        sendDirectory(msg.from,
                      net::discovery::encodeRegisterAck({.accepted = false,
                                                         .serverId = 0,
                                                         .publicHost = host,
                                                         .message = "server id belongs to another endpoint"}));
        return;
    }
    if (existing == servers_.end() && servers_.size() >= k_maxAdvertisedServers) {
        sendDirectory(msg.from,
                      net::discovery::encodeRegisterAck(
                          {.accepted = false, .serverId = 0, .publicHost = host, .message = "directory is full"}));
        return;
    }

    ServerRecord& record = existing != servers_.end() ? existing->second : servers_.try_emplace(id).first->second;

    record.info.id = id;
    record.info.name = sanitizeServerName(reg.name);
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
    active.reserve(std::min(servers_.size(), k_maxServersPerList));
    for (const auto& [_, record] : servers_) {
        if (active.size() >= k_maxServersPerList)
            break;
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

    net::discovery::PunchResponse resp;
    if (maybeReq->serverId == 0 || maybeReq->clientNonce == 0) {
        resp.message = "invalid punch request";
        sendDirectory(msg.from, net::discovery::encodePunchResponse(resp));
        return;
    }

    auto serverIt = servers_.find(maybeReq->serverId);
    if (serverIt == servers_.end()) {
        resp.message = "server not found";
        sendDirectory(msg.from, net::discovery::encodePunchResponse(resp));
        return;
    }

    const std::uint64_t key = relaySessionKey(maybeReq->serverId, maybeReq->clientNonce);
    if (clientsByRelaySession_.find(key) == clientsByRelaySession_.end() &&
        clientsByRelaySession_.size() >= k_maxRelayClients)
    {
        resp.message = "relay is busy";
        sendDirectory(msg.from, net::discovery::encodePunchResponse(resp));
        return;
    }

    ClientEndpoint& client = clientsByRelaySession_[key];
    client.endpoint = msg.from;
    client.relayToken = makeRelayToken(maybeReq->serverId, maybeReq->clientNonce, SDL_GetTicks());
    client.lastSeenMs = SDL_GetTicks();

    resp.accepted = true;
    resp.server = serverIt->second.info;
    resp.relayToken = client.relayToken;
    resp.message = "ok";

    const net::discovery::UdpPunchPeer toServer{
        .clientNonce = maybeReq->clientNonce,
        .host = addressString(msg.from.addr),
        .port = msg.from.port,
    };
    sendDirectory(serverIt->second.endpoint,
                  net::discovery::makeEnvelope(net::discovery::DirectoryMessage::PunchPeer,
                                               net::discovery::encodeUdpPunchPeer(toServer)));

    sendDirectory(msg.from, net::discovery::encodePunchResponse(resp));
}

void GlobalDirectoryServer::handleRelayPayload(const net::UdpReceivedMessage& msg)
{
    constexpr std::size_t k_tokenOffset = sizeof(std::uint32_t) * 2;
    constexpr std::size_t k_macOffset = k_tokenOffset + sizeof(std::uint64_t);
    constexpr std::size_t k_innerLenOffset = k_macOffset + net::k_relayTokenMacBytes;
    constexpr std::size_t k_relayEnvelopeBytes = k_innerLenOffset + sizeof(std::uint16_t);
    if (msg.payload.size() < k_relayEnvelopeBytes)
        return;

    const std::uint32_t serverId = net::readU32Le(msg.payload.data());
    const std::uint32_t clientNonce = net::readU32Le(msg.payload.data() + 4);
    const std::uint16_t innerLen = net::readU16Le(msg.payload.data() + k_innerLenOffset);
    if (serverId == 0 || clientNonce == 0 || innerLen < sizeof(net::PacketHeader) ||
        msg.payload.size() != k_relayEnvelopeBytes + innerLen)
    {
        return;
    }

    net::RelayToken relayToken;
    relayToken.expiresAtMs = net::readU64Le(msg.payload.data() + k_tokenOffset);
    std::memcpy(relayToken.mac.data(), msg.payload.data() + k_macOffset, relayToken.mac.size());
    auto serverIt = servers_.find(serverId);
    if (serverIt == servers_.end())
        return;

    ServerRecord& server = serverIt->second;
    const bool fromServer = sameEndpoint(msg.from, server.endpoint);
    const std::uint64_t key = relaySessionKey(serverId, clientNonce);
    if (fromServer) {
        auto clientIt = clientsByRelaySession_.find(key);
        if (clientIt == clientsByRelaySession_.end())
            return;
        sendRelay(clientIt->second.endpoint, msg.payload.data(), msg.payload.size());
    } else {
        auto clientIt = clientsByRelaySession_.find(key);
        if (clientIt == clientsByRelaySession_.end() ||
            ((!clientIt->second.relayAuthorized || !sameEndpoint(msg.from, clientIt->second.endpoint)) &&
             !validateRelayToken(relayToken, serverId, clientNonce, SDL_GetTicks())))
        {
            return;
        }
        clientIt->second.endpoint = msg.from;
        clientIt->second.lastSeenMs = SDL_GetTicks();
        clientIt->second.relayAuthorized = true;
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

net::RelayToken
GlobalDirectoryServer::makeRelayToken(std::uint32_t serverId, std::uint32_t clientNonce, Uint64 nowMs) const
{
    static constexpr Uint64 k_tokenTtlMs = 30000;
    net::RelayToken token;
    token.expiresAtMs = nowMs + k_tokenTtlMs;

    std::vector<std::uint8_t> material;
    appendTokenMaterial(material, serverId, clientNonce, token.expiresAtMs);
    token.mac = net::crypto::hmacSha256(relaySecret_.data(), relaySecret_.size(), material.data(), material.size());
    return token;
}

bool GlobalDirectoryServer::validateRelayToken(const net::RelayToken& token,
                                               std::uint32_t serverId,
                                               std::uint32_t clientNonce,
                                               Uint64 nowMs) const
{
    if (!net::hasRelayToken(token) || token.expiresAtMs < nowMs)
        return false;

    std::vector<std::uint8_t> material;
    appendTokenMaterial(material, serverId, clientNonce, token.expiresAtMs);
    const auto expected =
        net::crypto::hmacSha256(relaySecret_.data(), relaySecret_.size(), material.data(), material.size());
    return net::crypto::constantTimeEqual(token.mac.data(), expected.data(), expected.size());
}
