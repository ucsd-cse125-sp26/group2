/// @file GlobalDiscoveryProtocol.cpp
/// @brief Shared wire helpers for global server browser and NAT assist.

#include "GlobalDiscoveryProtocol.hpp"

#include <algorithm>
#include <cstring>
#include <random>

namespace net::discovery
{
namespace
{
inline constexpr std::uint32_t k_magic = 0x32444747u; // "GGD2"
inline constexpr std::uint16_t k_version = 1;

template <typename T>
void append(std::vector<std::uint8_t>& out, T value)
{
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

void appendString(std::vector<std::uint8_t>& out, const std::string& value)
{
    const auto len = static_cast<std::uint16_t>(std::min<std::size_t>(value.size(), 65535));
    append(out, len);
    out.insert(out.end(), value.data(), value.data() + len);
}

class Reader
{
public:
    Reader(const std::uint8_t* bytes, std::size_t length) : data(bytes), len(length) {}

    template <typename T>
    bool read(T& out)
    {
        if (offset + sizeof(T) > len)
            return false;
        std::memcpy(&out, data + offset, sizeof(T));
        offset += sizeof(T);
        return true;
    }

    bool readString(std::string& out)
    {
        std::uint16_t strLen = 0;
        if (!read(strLen) || offset + strLen > len)
            return false;
        out.assign(reinterpret_cast<const char*>(data + offset), strLen);
        offset += strLen;
        return true;
    }

    [[nodiscard]] bool finished() const { return offset == len; }

private:
    const std::uint8_t* data = nullptr;
    std::size_t len = 0;
    std::size_t offset = 0;
};

void appendServer(std::vector<std::uint8_t>& out, const ServerInfo& server)
{
    append(out, server.id);
    appendString(out, server.name);
    appendString(out, server.host);
    append(out, server.gamePort);
    appendString(out, server.udpHost);
    append(out, server.udpPort);
    append(out, server.currentPlayers);
    append(out, server.maxPlayers);
    append(out, server.lastSeenMs);
    append(out, static_cast<std::uint8_t>(server.natTraversalReady ? 1 : 0));
}

bool readServer(Reader& reader, ServerInfo& server)
{
    std::uint8_t natReady = 0;
    if (!reader.read(server.id) || !reader.readString(server.name) || !reader.readString(server.host) ||
        !reader.read(server.gamePort) || !reader.readString(server.udpHost) || !reader.read(server.udpPort) ||
        !reader.read(server.currentPlayers) || !reader.read(server.maxPlayers) || !reader.read(server.lastSeenMs) ||
        !reader.read(natReady))
    {
        return false;
    }
    server.natTraversalReady = natReady != 0;
    return true;
}
} // namespace

std::vector<std::uint8_t> makeEnvelope(DirectoryMessage kind, const std::vector<std::uint8_t>& payload)
{
    std::vector<std::uint8_t> out;
    out.reserve(sizeof(k_magic) + sizeof(k_version) + 1 + payload.size());
    append(out, k_magic);
    append(out, k_version);
    append(out, static_cast<std::uint8_t>(kind));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bool parseEnvelope(
    const void* data, std::size_t len, DirectoryMessage& kind, const std::uint8_t*& payload, std::size_t& payloadLen)
{
    if (len < sizeof(k_magic) + sizeof(k_version) + 1)
        return false;

    Reader reader(static_cast<const std::uint8_t*>(data), len);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint8_t rawKind = 0;
    if (!reader.read(magic) || !reader.read(version) || !reader.read(rawKind))
        return false;
    if (magic != k_magic || version != k_version)
        return false;

    kind = static_cast<DirectoryMessage>(rawKind);
    payload = static_cast<const std::uint8_t*>(data) + sizeof(k_magic) + sizeof(k_version) + 1;
    payloadLen = len - (sizeof(k_magic) + sizeof(k_version) + 1);
    return true;
}

std::vector<std::uint8_t> encodeRegistration(DirectoryMessage kind, const ServerRegistration& reg)
{
    std::vector<std::uint8_t> payload;
    append(payload, reg.serverId);
    appendString(payload, reg.name);
    append(payload, reg.gamePort);
    append(payload, reg.currentPlayers);
    append(payload, reg.maxPlayers);
    return makeEnvelope(kind, payload);
}

std::optional<ServerRegistration> decodeRegistration(const std::uint8_t* data, std::size_t len)
{
    Reader reader(data, len);
    ServerRegistration reg;
    if (!reader.read(reg.serverId) || !reader.readString(reg.name) || !reader.read(reg.gamePort) ||
        !reader.read(reg.currentPlayers) || !reader.read(reg.maxPlayers) || !reader.finished())
    {
        return std::nullopt;
    }
    return reg;
}

std::vector<std::uint8_t> encodeRegisterAck(const RegisterAck& ack)
{
    std::vector<std::uint8_t> payload;
    append(payload, static_cast<std::uint8_t>(ack.accepted ? 1 : 0));
    append(payload, ack.serverId);
    appendString(payload, ack.publicHost);
    appendString(payload, ack.message);
    return makeEnvelope(DirectoryMessage::RegisterAck, payload);
}

std::optional<RegisterAck> decodeRegisterAck(const std::uint8_t* data, std::size_t len)
{
    Reader reader(data, len);
    RegisterAck ack;
    std::uint8_t accepted = 0;
    if (!reader.read(accepted) || !reader.read(ack.serverId) || !reader.readString(ack.publicHost) ||
        !reader.readString(ack.message) || !reader.finished())
    {
        return std::nullopt;
    }
    ack.accepted = accepted != 0;
    return ack;
}

std::vector<std::uint8_t> encodeServerList(const std::vector<ServerInfo>& servers)
{
    std::vector<std::uint8_t> payload;
    append(payload, static_cast<std::uint16_t>(std::min<std::size_t>(servers.size(), 65535)));
    for (const ServerInfo& server : servers)
        appendServer(payload, server);
    return makeEnvelope(DirectoryMessage::ListResponse, payload);
}

std::optional<std::vector<ServerInfo>> decodeServerList(const std::uint8_t* data, std::size_t len)
{
    Reader reader(data, len);
    std::uint16_t count = 0;
    if (!reader.read(count))
        return std::nullopt;

    std::vector<ServerInfo> servers;
    servers.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        ServerInfo server;
        if (!readServer(reader, server))
            return std::nullopt;
        servers.push_back(std::move(server));
    }

    if (!reader.finished())
        return std::nullopt;
    return servers;
}

std::vector<std::uint8_t> encodePunchRequest(const PunchRequest& req)
{
    std::vector<std::uint8_t> payload;
    append(payload, req.serverId);
    append(payload, req.clientNonce);
    return makeEnvelope(DirectoryMessage::PunchRequest, payload);
}

std::optional<PunchRequest> decodePunchRequest(const std::uint8_t* data, std::size_t len)
{
    Reader reader(data, len);
    PunchRequest req;
    if (!reader.read(req.serverId) || !reader.read(req.clientNonce) || !reader.finished())
        return std::nullopt;
    return req;
}

std::vector<std::uint8_t> encodePunchResponse(const PunchResponse& resp)
{
    std::vector<std::uint8_t> payload;
    append(payload, static_cast<std::uint8_t>(resp.accepted ? 1 : 0));
    appendServer(payload, resp.server);
    append(payload, resp.relayToken);
    appendString(payload, resp.message);
    return makeEnvelope(DirectoryMessage::PunchResponse, payload);
}

std::optional<PunchResponse> decodePunchResponse(const std::uint8_t* data, std::size_t len)
{
    Reader reader(data, len);
    PunchResponse resp;
    std::uint8_t accepted = 0;
    if (!reader.read(accepted) || !readServer(reader, resp.server) || !reader.read(resp.relayToken) ||
        !reader.readString(resp.message) || !reader.finished())
    {
        return std::nullopt;
    }
    resp.accepted = accepted != 0;
    return resp;
}

std::vector<std::uint8_t> encodeUdpHello(const UdpHello& hello)
{
    std::vector<std::uint8_t> payload;
    append(payload, static_cast<std::uint8_t>(DirectoryUdpMessage::Hello));
    append(payload, static_cast<std::uint8_t>(hello.role));
    append(payload, hello.idOrNonce);
    append(payload, hello.gamePort);
    return payload;
}

std::optional<UdpHello> decodeUdpHello(const std::uint8_t* data, std::size_t len)
{
    Reader reader(data, len);
    std::uint8_t kind = 0;
    std::uint8_t role = 0;
    UdpHello hello;
    if (!reader.read(kind) || kind != static_cast<std::uint8_t>(DirectoryUdpMessage::Hello) || !reader.read(role) ||
        !reader.read(hello.idOrNonce) || !reader.read(hello.gamePort) || !reader.finished())
    {
        return std::nullopt;
    }
    hello.role = static_cast<UdpRole>(role);
    return hello;
}

std::vector<std::uint8_t> encodeUdpPunchPeer(const UdpPunchPeer& peer)
{
    std::vector<std::uint8_t> payload;
    append(payload, static_cast<std::uint8_t>(DirectoryUdpMessage::PunchPeer));
    append(payload, peer.clientNonce);
    appendString(payload, peer.host);
    append(payload, peer.port);
    return payload;
}

std::optional<UdpPunchPeer> decodeUdpPunchPeer(const std::uint8_t* data, std::size_t len)
{
    Reader reader(data, len);
    std::uint8_t kind = 0;
    UdpPunchPeer peer;
    if (!reader.read(kind) || kind != static_cast<std::uint8_t>(DirectoryUdpMessage::PunchPeer) ||
        !reader.read(peer.clientNonce) || !reader.readString(peer.host) || !reader.read(peer.port) ||
        !reader.finished())
    {
        return std::nullopt;
    }
    return peer;
}

std::uint32_t randomNonce()
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint32_t> dist{1, UINT32_MAX};
    return dist(rng);
}

} // namespace net::discovery
