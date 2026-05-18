/// @file GlobalDirectoryServer.hpp
/// @brief UDP-only central directory, NAT assist, and relay service.

#pragma once

#include "network/discovery/GlobalDiscoveryProtocol.hpp"
#include "network/transport/UdpEndpoint.hpp"

#include <SDL3/SDL_stdinc.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

class GlobalDirectoryServer
{
public:
    /// @brief Start the UDP-only directory/relay service.
    ///
    /// @param bindHost Optional bind host. nullptr means all interfaces.
    /// @param tcpPort  Ignored legacy argument, kept so old launch scripts
    ///                 that pass "tcp udp" still work during cutover.
    /// @param udpPort  Public UDP directory/relay port.
    bool init(const char* bindHost, Uint16 tcpPort, Uint16 udpPort);
    void run();
    void stop();

private:
    struct ServerRecord
    {
        net::discovery::ServerInfo info;
        Uint64 lastSeenMs = 0;
        net::UdpEndpointAddr endpoint;
    };

    struct ClientEndpoint
    {
        net::UdpEndpointAddr endpoint;
        std::uint64_t relayToken = 0;
        Uint64 lastSeenMs = 0;
    };

    void pollUdp();
    void pruneExpired();
    void handleDirectoryControl(const net::UdpReceivedMessage& msg);
    void handleRegistration(const net::UdpReceivedMessage& msg,
                            net::discovery::DirectoryMessage kind,
                            const std::uint8_t* data,
                            std::size_t len);
    void handleListRequest(const net::UdpReceivedMessage& msg);
    void handlePunchRequest(const net::UdpReceivedMessage& msg, const std::uint8_t* data, std::size_t len);
    void handleRelayPayload(const net::UdpReceivedMessage& msg);
    void sendDirectory(const net::UdpEndpointAddr& dest, const std::vector<std::uint8_t>& payload);
    void sendRelay(const net::UdpEndpointAddr& dest, const std::uint8_t* payload, std::size_t len);

    net::UdpEndpoint udpEndpoint_;
    std::unordered_map<std::uint32_t, ServerRecord> servers_;
    std::unordered_map<std::uint32_t, ClientEndpoint> clientsByNonce_;
    std::uint32_t nextServerId_ = 1;
    std::atomic<bool> shouldStop_{false};
};
