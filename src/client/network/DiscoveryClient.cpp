#include "DiscoveryClient.hpp"

#include "SDL3_net/SDL_net.h"
#include "network/PacketType.hpp"

#include <cstring>
#include <vector>

bool DiscoveryClient::start(uint16_t port)
{
    discoveryPort = port;

    socket = NET_CreateDatagramSocket(nullptr, 0);
    if (!socket) {
        SDL_Log("DiscoveryClient: failed to create datagram socket: %s", SDL_GetError());
        return false;
    }

    broadcastAddr = NET_ResolveHostname("255.255.255.255");
    if (NET_WaitUntilResolved(broadcastAddr, -1) == NET_FAILURE) {
        SDL_Log("DiscoveryClient: failed to resolve broadcast address: %s", SDL_GetError());
        NET_UnrefAddress(broadcastAddr);
        NET_DestroyDatagramSocket(socket);
        socket = nullptr;
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
    if (broadcastAddr) {
        NET_UnrefAddress(broadcastAddr);
        broadcastAddr = nullptr;
    }
}

void DiscoveryClient::poll()
{
    if (!socket) {
        return;
    }

    auto now = SDL_GetTicks();
    // broadcast every 1 second
    if (broadcastAddr && now - lastRequestMs > 1000) {
        uint8_t buf[1] = {static_cast<uint8_t>(PacketType::LOCAL_SERVER_DISCOVERY_REQUEST)};
        NET_SendDatagram(socket, broadcastAddr, discoveryPort, buf, sizeof(buf));
        lastRequestMs = now;
    }

    NET_Datagram* datagram = nullptr;
    while (NET_ReceiveDatagram(socket, &datagram) && datagram) {
        // min length is 1+2+1+1+1=6
        if (datagram->buflen >= 6 &&
            static_cast<PacketType>(datagram->buf[0]) == PacketType::LOCAL_SERVER_DISCOVERY_RESPONSE)
        {
            const uint8_t* data = datagram->buf + 1;

            uint16_t port = 0;
            std::memcpy(&port, data, sizeof(port));
            data += sizeof(port);

            uint8_t currentPlayers = *data++;
            uint8_t maxPlayers = *data++;
            uint8_t nameLength = *data++;

            if (datagram->buflen >= 6 + nameLength) {
                const char* addrRaw = NET_GetAddressString(datagram->addr);
                std::string addr = addrRaw ? addrRaw : "unknown";
                std::string fullAddr = addr + ":" + std::to_string(port);

                auto& server = discoveredServers[fullAddr];
                server.hostIp = addr;
                server.gamePort = port;
                server.currentPlayers = currentPlayers;
                server.maxPlayers = maxPlayers;
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
