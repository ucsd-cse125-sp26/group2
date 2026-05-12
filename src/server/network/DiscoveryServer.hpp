#pragma once

#include "SDL3_net/SDL_net.h"

#include <atomic>
#include <mutex>
#include <thread>
class DiscoveryServer
{

public:
    struct ServerInfo
    {
        std::string serverName;
        uint16_t gamePort;
        uint8_t currentPlayers;

        // other interesting things can go here
    };

    /// starts a thread that makes a UDP socket and broadcasts
    bool start(uint16_t port, const ServerInfo& serverInfo);

    void updateInfo(const ServerInfo& serverInfo);

    void stop();

private:
    void loop();

    NET_DatagramSocket* socket = nullptr;
    NET_Address* broadcastAddr = nullptr;
    uint16_t discoveryPort = 0;

    std::atomic<bool> shouldStop{false};
    std::thread broadcastThread;

    ServerInfo info;
    std::mutex infoMutex;
};
