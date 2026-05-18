#pragma once

#include "SDL3_net/SDL_net.h"

#include <string>
#include <unordered_map>
#include <vector>

class DiscoveryClient
{
public:
    struct DiscoveredServer
    {
        std::string serverName;
        std::string hostIp;
        uint16_t gamePort;
        uint8_t currentPlayers;
        uint8_t maxPlayers;
        uint64_t lastSeenMs;
    };

    bool start(uint16_t discoveryPort);

    void stop();

    void poll();

    std::vector<DiscoveredServer> getServers();

private:
    uint16_t discoveryPort = 0;
    NET_Address *broadcastAddr = nullptr;
    uint64_t lastRequestMs = 0;

    NET_DatagramSocket* socket = nullptr;

    std::unordered_map<std::string, DiscoveredServer> discoveredServers; // key is host IP
};
