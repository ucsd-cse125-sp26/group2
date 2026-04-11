/// @file Client.cpp
/// @brief Implementation of the TCP client connection and message I/O.

#include "Client.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>

#include <SDL3_net/SDL_net.h>

bool Client::init(const char* addr, Uint16 port)
{
    serverAddr = NET_ResolveHostname(addr);
    if (NET_WaitUntilResolved(serverAddr, -1) == NET_FAILURE) {
        SDL_Log("Failed to resolve server address: %s", SDL_GetError());
        return false;
    }

    auto sock = NET_CreateClient(serverAddr, port);
    if (!sock) {
        SDL_Log("Failed to create client %s", SDL_GetError());
        return false;
    }

    if (NET_WaitUntilConnected(sock, -1) == NET_FAILURE) {
        SDL_Log("Client: connection failed: %s", SDL_GetError());
        NET_DestroyStreamSocket(sock);
        sock = nullptr;
        return false;
    }

    msgStream.socket = sock;

    SDL_Log("Client created, server address is %s", NET_GetAddressString(serverAddr));
    return true;
}

void Client::shutdown()
{
    if (msgStream.socket) {
        NET_DestroyStreamSocket(msgStream.socket);
        msgStream.socket = nullptr;
    }

    if (serverAddr) {
        NET_UnrefAddress(serverAddr);
        serverAddr = nullptr;
    }
}

bool Client::send(const void* data, int len)
{
    auto msgLen = static_cast<Uint32>(len);
    NET_WriteToStreamSocket(msgStream.socket, &msgLen, sizeof(msgLen));
    return NET_WriteToStreamSocket(msgStream.socket, data, len);
}

bool Client::poll()
{
    msgStream.poll([](const void* data, Uint32 size) {
        SDL_Log("Received (%d bytes): %.*s", size, size, reinterpret_cast<const char*>(data));
    });
    return false;
}
