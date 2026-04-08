#include "Server.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

bool Server::init(const char* addr, Uint16 port)
{
    NET_Address* netAddr = NET_ResolveHostname(addr);
    while (NET_GetAddressStatus(netAddr) == NET_WAITING) {
        SDL_Delay(100);
    }
    if (NET_GetAddressStatus(netAddr) == NET_FAILURE) {
        SDL_Log("Server: failed to resolve address: %s", SDL_GetError());
        NET_UnrefAddress(netAddr);
        return false;
    }

    sock = NET_CreateDatagramSocket(netAddr, port);
    NET_UnrefAddress(netAddr);
    if (!sock) {
        SDL_Log("Server: failed to create socket: %s", SDL_GetError());
        return false;
    }

    SDL_Log("Server: listening on port %d", (int)port);
    return true;
}

void Server::shutdown()
{
    if (sock) {
        SDL_Log("Server: shutting down");
        NET_DestroyDatagramSocket(sock);
        sock = nullptr;
    }
}

void Server::poll()
{
    NET_Datagram* dgram = nullptr;
    while (NET_ReceiveDatagram(sock, &dgram) && dgram) {
        handleDatagram(dgram);
        NET_DestroyDatagram(dgram);
        dgram = nullptr;
    }
}

void Server::handleDatagram(NET_Datagram* dgram)
{
    const char* addr = NET_GetAddressString(dgram->addr);
    SDL_Log("Server: received %d bytes from %s:%d", dgram->buflen, addr, dgram->port);
    SDL_Log("Server: data: %.*s", dgram->buflen, (const char*)dgram->buf);

    // Echo back to sender.
    NET_SendDatagram(sock, dgram->addr, dgram->port, dgram->buf, dgram->buflen);
}
