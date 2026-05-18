/// @file GlobalDiscoveryProtocol.hpp
/// @brief Shared wire helpers for global server browser and NAT assist.

#pragma once

#include "network/RelayToken.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace net::discovery
{

inline constexpr std::uint16_t k_defaultDirectoryTcpPort = 10080;
inline constexpr std::uint16_t k_defaultDirectoryUdpPort = 10081;
inline constexpr std::uint64_t k_serverTtlMs = 15000;

enum class DirectoryMessage : std::uint8_t
{
    RegisterServer = 1,
    RegisterAck = 2,
    Heartbeat = 3,
    ListRequest = 4,
    ListResponse = 5,
    PunchRequest = 6,
    PunchResponse = 7,
    PunchPeer = 8,
};

enum class DirectoryUdpMessage : std::uint8_t
{
    Hello = 1,
    PunchPeer = 2,
    PunchProbe = 3,
};

enum class UdpRole : std::uint8_t
{
    Server = 1,
    Client = 2,
};

struct ServerInfo
{
    std::uint32_t id = 0;
    std::string name;
    std::string host;
    std::uint16_t gamePort = 0;
    std::string udpHost;
    std::uint16_t udpPort = 0;
    std::uint8_t currentPlayers = 0;
    std::uint8_t maxPlayers = 0;
    std::uint64_t lastSeenMs = 0;
    bool natTraversalReady = false;
};

struct ServerRegistration
{
    std::uint32_t serverId = 0;
    std::string name;
    std::uint16_t gamePort = 0;
    std::uint8_t currentPlayers = 0;
    std::uint8_t maxPlayers = 0;
};

struct RegisterAck
{
    bool accepted = false;
    std::uint32_t serverId = 0;
    std::string publicHost;
    std::string message;
};

struct PunchRequest
{
    std::uint32_t serverId = 0;
    std::uint32_t clientNonce = 0;
};

struct PunchResponse
{
    bool accepted = false;
    ServerInfo server;
    net::RelayToken relayToken;
    std::string message;
};

struct UdpHello
{
    UdpRole role = UdpRole::Client;
    std::uint32_t idOrNonce = 0;
    std::uint16_t gamePort = 0;
};

struct UdpPunchPeer
{
    std::uint32_t clientNonce = 0;
    std::string host;
    std::uint16_t port = 0;
};

std::vector<std::uint8_t> makeEnvelope(DirectoryMessage kind, const std::vector<std::uint8_t>& payload = {});
bool parseEnvelope(
    const void* data, std::size_t len, DirectoryMessage& kind, const std::uint8_t*& payload, std::size_t& payloadLen);

std::vector<std::uint8_t> encodeRegistration(DirectoryMessage kind, const ServerRegistration& reg);
std::optional<ServerRegistration> decodeRegistration(const std::uint8_t* data, std::size_t len);

std::vector<std::uint8_t> encodeRegisterAck(const RegisterAck& ack);
std::optional<RegisterAck> decodeRegisterAck(const std::uint8_t* data, std::size_t len);

std::vector<std::uint8_t> encodeServerList(const std::vector<ServerInfo>& servers);
std::optional<std::vector<ServerInfo>> decodeServerList(const std::uint8_t* data, std::size_t len);

std::vector<std::uint8_t> encodePunchRequest(const PunchRequest& req);
std::optional<PunchRequest> decodePunchRequest(const std::uint8_t* data, std::size_t len);

std::vector<std::uint8_t> encodePunchResponse(const PunchResponse& resp);
std::optional<PunchResponse> decodePunchResponse(const std::uint8_t* data, std::size_t len);

std::vector<std::uint8_t> encodeUdpHello(const UdpHello& hello);
std::optional<UdpHello> decodeUdpHello(const std::uint8_t* data, std::size_t len);

std::vector<std::uint8_t> encodeUdpPunchPeer(const UdpPunchPeer& peer);
std::optional<UdpPunchPeer> decodeUdpPunchPeer(const std::uint8_t* data, std::size_t len);

std::uint32_t randomNonce();

} // namespace net::discovery
