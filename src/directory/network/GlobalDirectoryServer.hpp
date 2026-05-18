/// @file GlobalDirectoryServer.hpp
/// @brief Central directory server for global server browser and NAT assist.

#pragma once

#include "network/MessageStream.hpp"
#include "network/discovery/GlobalDiscoveryProtocol.hpp"
#include "network/transport/UdpEndpoint.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class GlobalDirectoryServer
{
public:
    bool init(const char* bindHost, Uint16 tcpPort, Uint16 udpPort);
    void run();
    void stop();

private:
    struct TcpConnection
    {
        MessageStream stream;
        std::string host;
    };

    struct ServerRecord
    {
        net::discovery::ServerInfo info;
        Uint64 lastSeenMs = 0;
    };

    struct ClientUdpEndpoint
    {
        std::string host;
        Uint16 port = 0;
        Uint64 lastSeenMs = 0;
    };

    void acceptClients();
    void pollTcpClients();
    void pollUdp();
    void pruneExpired();

    void handleTcpMessage(TcpConnection& conn, const void* data, Uint32 len);
    void handleRegistration(TcpConnection& conn,
                            net::discovery::DirectoryMessage kind,
                            const std::uint8_t* data,
                            std::size_t len);
    void handleListRequest(TcpConnection& conn);
    void handlePunchRequest(TcpConnection& conn, const std::uint8_t* data, std::size_t len);
    void sendTcp(TcpConnection& conn, const std::vector<std::uint8_t>& payload);
    void sendUdpTo(const std::string& host,
                   Uint16 port,
                   const std::vector<std::uint8_t>& payload,
                   std::uint32_t connectionId);

    NET_Server* tcpServer_ = nullptr;
    net::UdpEndpoint udpEndpoint_;
    std::vector<TcpConnection> clients_;
    std::unordered_map<std::uint32_t, ServerRecord> servers_;
    std::unordered_map<std::uint32_t, ClientUdpEndpoint> clientUdpByNonce_;
    std::uint32_t nextServerId_ = 1;
    std::atomic<bool> shouldStop_{false};
};
