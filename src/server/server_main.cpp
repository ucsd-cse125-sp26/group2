#include "Server.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

int main()
{
    SDL_Init(0);
    NET_Init();

    int tickRateMs = 30;

    Server server;
    if (!server.init("127.0.0.1", 9999, tickRateMs)) {
        return 1;
    }

    server.run();
    server.shutdown();

    NET_Quit();
    SDL_Quit();
    return 0;
}