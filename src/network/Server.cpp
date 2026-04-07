#include "Server.hpp"

#include "SDL3/SDL_stdinc.h"

#include <SDL3/SDL.H>

#include <SDL3_net/SDL_net.h>

bool Server::init(NET_Address* addr, Uint16 port)
{
    server = NET_CreateServer(addr, port);
    if (!server) {
        SDL_Log("Failed to create server %s", SDL_GetError());
        return false;
    }

    SDL_Log("Server created on port %d", (int)port);
    return true;
}

void Server::run()
{
    // Set server socket to 0
    int num_vsockets = 1;
    void* vsockets[128];
    SDL_zeroa(vsockets);
    vsockets[0] = server;

    // Server loop
    while (NET_WaitUntilInputAvailable(vsockets, num_vsockets, -1) > 0) {
        // Check for new clients
        NET_StreamSocket* streamsocket = nullptr;
        if (!NET_AcceptClient(server, &streamsocket)) {
            SDL_Log("NET_AcceptClient failed: %s", SDL_GetError());
            break;
        } else if (streamsocket) {
            SDL_Log("Net connection from %s", NET_GetAddressString(NET_GetStreamSocketAddress(streamsocket)));
            if (num_vsockets >= (int)(SDL_arraysize(vsockets) - 1)) {
                SDL_Log("  (too many connections, though, so dropping immediately.)");
                NET_DestroyStreamSocket(streamsocket);
            } else {
                vsockets[num_vsockets++] = streamsocket;
            }
        }

        // Check for input from clients
        char buffer[1024];
        for (int i = 1; i < num_vsockets; i++) {
            bool kill_socket = false;
            streamsocket = (NET_StreamSocket*)vsockets[i];
            const int br = NET_ReadFromStreamSocket(streamsocket, buffer, sizeof(buffer));
            if (br < 0) {
                SDL_Log("NET_ReadFromStreamSocket failed: %s", SDL_GetError());
                kill_socket = true;
            } else if (br > 0) {
                const char* addr = NET_GetAddressString(NET_GetStreamSocketAddress(streamsocket));
                SDL_Log("Got %d more bytes from '%s'", br, addr);

                // Echo data back to client
                if (!NET_WriteToStreamSocket(streamsocket, buffer, br)) {
                    SDL_Log("Failed to echo data back to '%s': %s", addr, SDL_GetError());
                    kill_socket = true;
                }
            }

            if (kill_socket) {
                SDL_Log("Dropping connection to '%s'", NET_GetAddressString(NET_GetStreamSocketAddress(streamsocket)));
                NET_DestroyStreamSocket(streamsocket);
                vsockets[i] = nullptr;
                if (i < (num_vsockets - 1)) {
                    SDL_memmove(&vsockets[i], &vsockets[i + 1], sizeof(vsockets[0]) * ((num_vsockets - i) - 1));
                }
                num_vsockets--;
                i--;
            }
        }
    }
}

void Server::shutdown()
{
    if (server) {
        SDL_Log("Shutting down server");
        NET_DestroyServer(server);
        server = nullptr;
    }
}