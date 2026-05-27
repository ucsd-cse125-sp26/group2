#pragma once

#include "SDL3_net/SDL_net.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
class DiscoveryServer
{

public:
    struct ServerInfo
    {
        std::string serverName;
        uint16_t gamePort;
        uint8_t currentPlayers;
        uint8_t maxPlayers;
        uint32_t globalServerId = 0;

        // other interesting things can go here
    };

    /// starts a thread that makes a UDP socket and broadcasts
    bool start(uint16_t port,
               const ServerInfo& serverInfo,
               std::function<uint8_t()> playerCountFn = nullptr,
               std::function<uint32_t()> globalServerIdFn = nullptr);

    void updateInfo(const ServerInfo& serverInfo);

    void stop();

private:
    void loop();

    NET_DatagramSocket* socket = nullptr;

    std::atomic<bool> shouldStop{false};
    std::thread broadcastThread;

    ServerInfo info;
    std::mutex infoMutex;

    std::function<uint8_t()> currentPlayersFn;
    std::function<uint32_t()> globalServerIdFn;
};
