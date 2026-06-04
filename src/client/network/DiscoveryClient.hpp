#pragma once

#include "SDL3_net/SDL_net.h"

#include <cstdint>
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
        uint32_t globalServerId = 0;
        uint64_t lastSeenMs;
    };

    bool start(uint16_t discoveryPort);

    void stop();

    void poll();

    /// @brief Immediately send a LAN discovery request.
    /// @param clearExisting True when starting a user-requested fresh scan.
    void refresh(bool clearExisting = false);

    std::vector<DiscoveredServer> getServers();

private:
    uint16_t discoveryPort = 0;
    NET_Address* broadcastAddr = nullptr;
    NET_Address* loopbackAddr = nullptr;
    std::vector<NET_Address*> subnetBroadcastAddrs;
    uint64_t lastRequestMs = 0;

    NET_DatagramSocket* socket = nullptr;

    std::unordered_map<std::string, DiscoveredServer> discoveredServers; // key is host:port
};
