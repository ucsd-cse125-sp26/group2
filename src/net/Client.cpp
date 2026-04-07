#include "Client.hpp"

#include "SDL3/SDL_timer.h"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

bool Client::init(NET_Address* addr, Uint16 port)
{
    server_port = port;
    sock = NET_CreateDatagramSocket(nullptr, 0);
    if (!sock) {
        SDL_Log("Failed to create client %s", SDL_GetError());
        return false;
    }

    server_addr = NET_ResolveHostname(NET_GetAddressString(addr));
    while (NET_GetAddressStatus(server_addr) == NET_WAITING) {
        SDL_Delay(100);
    }
    if (NET_GetAddressStatus(server_addr) == NET_FAILURE) {
        SDL_Log("Failed to resolve server address: %s", SDL_GetError());
        return false;
    }

    SDL_Log("Client created, server address is %s", NET_GetAddressString(server_addr));
    return true;
}

void Client::shutdown()
{
    if (sock) {
        NET_DestroyDatagramSocket(sock);
        sock = nullptr;
    }

    if (server_addr) {
        NET_UnrefAddress(server_addr);
        server_addr = nullptr;
    }
}

bool Client::send(const void* data, int len)
{
    return NET_SendDatagram(sock, server_addr, server_port, data, len);
}

bool Client::poll()
{
    NET_Datagram* dgram = nullptr;
    NET_ReceiveDatagram(sock, &dgram);
    if (dgram) {
        SDL_Log("Received (%d bytes): %.*s", dgram->buflen, dgram->buflen, (const char*)dgram->buf);
        NET_DestroyDatagram(dgram);
        return true;
    }

    return false;
}