#include "DiscoveryClient.hpp"

#include "SDL3_net/SDL_net.h"
#include "network/PacketType.hpp"

#include <vector>

bool DiscoveryClient::start(uint16_t discoveryPort)
{
    socket = NET_CreateDatagramSocket(nullptr, discoveryPort);
    if (!socket) {
        SDL_Log("DiscoveryClient: failed to create datagram socket: %s", SDL_GetError());
        return false;
    }
    return true;
}

void DiscoveryClient::stop()
{
    if (socket) {
        NET_DestroyDatagramSocket(socket);
        socket = nullptr;
    }
}

void DiscoveryClient::poll()
{
    if (!socket) {
        return;
    }

    NET_Datagram* datagram = nullptr;
    while (NET_ReceiveDatagram(socket, &datagram) && datagram) {
        // min length is 1+2+1+1=5
        if (datagram->buflen >= 5 && static_cast<PacketType>(datagram->buf[0]) == PacketType::LOCAL_SERVER_ADVERTISEMENT) {
            const uint8_t* data = datagram->buf + 1;

            uint16_t port = 0;
            std::memcpy(&port, data, sizeof(port));
            data += sizeof(port);

            uint8_t currentPlayers = *data++;
            uint8_t nameLength = *data++;

            if (datagram->buflen >= 5 + nameLength) {
                const char* addrRaw = NET_GetAddressString(datagram->addr);
                std::string addr = addrRaw ? addrRaw : "unknown";
                std::string fullAddr = addr + ":" + std::to_string(port);

                auto& server = discoveredServers[fullAddr];
                server.hostIp = addr;
                server.gamePort = port;
                server.currentPlayers = currentPlayers;
                server.serverName.assign(reinterpret_cast<const char*>(data), nameLength);
                server.lastSeenMs = SDL_GetTicks();
            }
        }

        NET_DestroyDatagram(datagram);
        datagram = nullptr;
    }
}

std::vector<DiscoveryClient::DiscoveredServer> DiscoveryClient::getServers()
{
    // timeout servers after 10 seconds
    constexpr uint64_t timeoutMs = 10000;
    const uint64_t now = SDL_GetTicks();
    for (auto it = discoveredServers.begin(); it != discoveredServers.end();) {
        if (now - it->second.lastSeenMs > timeoutMs) {
            it = discoveredServers.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<DiscoveredServer> servers;
    servers.reserve(discoveredServers.size());
    for (const auto& [_, server] : discoveredServers) {
        servers.push_back(server);
    }

    return servers;
}
