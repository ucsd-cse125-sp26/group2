#include "net/Server.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

int main()
{
    SDL_Init(0);
    NET_Init();

    NET_Address* addr = NET_ResolveHostname("127.0.0.1");
    while (NET_GetAddressStatus(addr) == NET_WAITING) {
        SDL_Delay(100);
    }
    if (NET_GetAddressStatus(addr) == NET_FAILURE) {
        SDL_Log("Failed to resolve address: %s", SDL_GetError());
        return 1;
    }

    Server server;
    if (!server.init(addr, 9999)) {
        return 1;
    }

    server.run();
    server.shutdown();

    NET_Quit();
    SDL_Quit();
    return 0;
}