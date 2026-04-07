#include "Server.hpp"

#include "SDL3/SDL_stdinc.h"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

bool Server::init(const char* addr, Uint16 port, Uint32 tickRateMs)
{
    serverTickRateMs = tickRateMs;
    NET_Address* netAddr = NET_ResolveHostname(addr);
    while (NET_GetAddressStatus(netAddr) == NET_WAITING) {
        SDL_Delay(100);
    }
    if (NET_GetAddressStatus(netAddr) == NET_FAILURE) {
        SDL_Log("Failed to resolve address: %s", SDL_GetError());
        return false;
    }

    sock = NET_CreateDatagramSocket(netAddr, port);
    if (!sock) {
        SDL_Log("Failed to create server socket: %s", SDL_GetError());
        return false;
    }

    SDL_Log("Server created on port %d", (int)port);
    return true;
}

void Server::shutdown()
{
    running = false;
    if (sock) {
        SDL_Log("Shutting down server");
        NET_DestroyDatagramSocket(sock);
        sock = nullptr;
    }
}

void Server::run()
{
    running = true;
    while (running) {
        NET_Datagram* dgram = nullptr;
        NET_ReceiveDatagram(sock, &dgram);

        if (dgram) {
            const char* addr = NET_GetAddressString(dgram->addr);
            SDL_Log("Received %d bytes from %s:%d", dgram->buflen, addr, dgram->port);
            SDL_Log("Data: %.*s", dgram->buflen, (const char*)dgram->buf);

            // Echo back
            NET_SendDatagram(sock, dgram->addr, dgram->port, dgram->buf, dgram->buflen);

            NET_DestroyDatagram(dgram);
        }

        SDL_Delay(serverTickRateMs);
    }
}
