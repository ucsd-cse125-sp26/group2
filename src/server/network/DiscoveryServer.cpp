#include "DiscoveryServer.hpp"

#include "SDL3_net/SDL_net.h"
#include "network/PacketType.hpp"

#include <cstring>
#include <mutex>
#include <utility>

// struct ServerInfo
// {
//     std::string serverName;
//     uint16_t gamePort;
//     uint8_t currentPlayers;
//     uint8_t maxPlayers;
//
//     // other interesting things can go here
// };
//
// /// starts a thread that makes a UDP socket and broadcasts

bool DiscoveryServer::start(uint16_t port, const ServerInfo& serverInfo, std::function<uint8_t()> playerCountFn)
{
    currentPlayersFn = std::move(playerCountFn);

    {
        std::lock_guard<std::mutex> lock(infoMutex);
        info = serverInfo;
    }

    socket = NET_CreateDatagramSocket(nullptr, port);
    if (!socket) {
        SDL_Log("DiscoveryServer: failed to create datagram socket: %s", SDL_GetError());
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
}

void DiscoveryServer::loop()
{
    while (!shouldStop.load()) {

        // serialize into packet
        std::vector<uint8_t> data;
        {
            std::lock_guard<std::mutex> lock(infoMutex);

            if (currentPlayersFn) {
                info.currentPlayers = currentPlayersFn();
            }

            const uint8_t nameLen = static_cast<uint8_t>(std::min<size_t>(info.serverName.size(), 255));

            // packet is
            // [packetType (1)][gamePort (2)][currentPlayers (1)][maxPlayers (1)][nameLen (1)][name (nameLen)]
            data.resize(1 + 4 + 1 + nameLen);

            uint8_t* ptr = data.data();
            *ptr++ = static_cast<uint8_t>(PacketType::LOCAL_SERVER_DISCOVERY_RESPONSE);

            std::memcpy(ptr, &info.gamePort, sizeof(info.gamePort));
            ptr += sizeof(info.gamePort);

            *ptr++ = info.currentPlayers;
            *ptr++ = info.maxPlayers;

            *ptr++ = nameLen;
            std::memcpy(ptr, info.serverName.data(), nameLen);
        }

        // drain datagrams
        NET_Datagram* datagram = nullptr;
        while (NET_ReceiveDatagram(socket, &datagram) && datagram) {
            // check if it is a server discovery request
            if (datagram->buflen >= 1 &&
                static_cast<PacketType>(datagram->buf[0]) == PacketType::LOCAL_SERVER_DISCOVERY_REQUEST)
            {
                // respond to the request
                NET_SendDatagram(socket, datagram->addr, datagram->port, data.data(), static_cast<int>(data.size()));
            }

            NET_DestroyDatagram(datagram);
        }

        SDL_Delay(50);
    }
}
