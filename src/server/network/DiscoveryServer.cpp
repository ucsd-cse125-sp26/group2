#include "DiscoveryServer.hpp"

#include "SDL3_net/SDL_net.h"
#include "network/PacketType.hpp"

#include <mutex>

// struct ServerInfo
// {
//     std::string serverName;
//     uint16_t gamePort;
//     uint8_t currentPlayers;
//
//     // other interesting things can go here
// };
//
// /// starts a thread that makes a UDP socket and broadcasts

bool DiscoveryServer::start(uint16_t port, const ServerInfo& serverInfo)
{
    discoveryPort = port;

    {
        std::lock_guard<std::mutex> lock(infoMutex);
        info = serverInfo;
    }

    socket = NET_CreateDatagramSocket(nullptr, 0);
    if (!socket) {
        SDL_Log("DiscoveryServer: failed to create datagram socket: %s", SDL_GetError());
        return false;
    }

    broadcastAddr = NET_ResolveHostname("255.255.255.255");
    if (NET_WaitUntilResolved(broadcastAddr, -1) == NET_FAILURE) {
        SDL_Log("DiscoveryServer: failed to resolve broadcast address: %s", SDL_GetError());
        NET_UnrefAddress(broadcastAddr);
        NET_DestroyDatagramSocket(socket);
        socket = nullptr;
        return false;
    }

    shouldStop.store(false);
    broadcastThread = std::thread(&DiscoveryServer::loop, this);
    return true;
}

void DiscoveryServer::updateInfo(const ServerInfo& serverInfo)
{
    std::lock_guard<std::mutex> lock(infoMutex);
    info = serverInfo;
}

void DiscoveryServer::stop()
{
    shouldStop.store(true);

    if (broadcastThread.joinable()) {
        broadcastThread.join();
    }

    if (socket) {
        NET_DestroyDatagramSocket(socket);
        socket = nullptr;
    }
    if (broadcastAddr) {
        NET_UnrefAddress(broadcastAddr);
        broadcastAddr = nullptr;
    }
}

void DiscoveryServer::loop()
{
    while (!shouldStop.load()) {
        // serialize into packet
        std::vector<uint8_t> data;
        {
            std::lock_guard<std::mutex> lock(infoMutex);

            const uint8_t nameLen = static_cast<uint8_t>(std::min<size_t>(info.serverName.size(), 255));

            // packet is
            // [packetType (1)][everything but name len (2+1 = 3)][nameLen (1)][name (nameLen)]
            data.resize(1 + 3 + 1 + nameLen);

            uint8_t* ptr = data.data();
            *ptr++ = static_cast<uint8_t>(PacketType::LOCAL_SERVER_ADVERTISEMENT);

            std::memcpy(ptr, &info.gamePort, sizeof(info.gamePort));
            ptr += sizeof(info.gamePort);

            *ptr++ = info.currentPlayers;

            *ptr++ = nameLen;
            std::memcpy(ptr, info.serverName.data(), nameLen);
        }

        NET_SendDatagram(socket, broadcastAddr, discoveryPort, data.data(), static_cast<int>(data.size()));

        // check should stop every 100ms, wait 2s
        for (int i = 0; i < 20 && !shouldStop.load(); ++i) {
            SDL_Delay(100);
        }
    }
}
