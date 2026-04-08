#include "game/ServerGame.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

int main()
{
    SDL_Init(0);
    NET_Init();

    ServerGame game;
    if (!game.init("127.0.0.1", 9999, /*tickRateMs=*/30)) {
        NET_Quit();
        SDL_Quit();
        return 1;
    }

    game.run();
    game.shutdown();

    NET_Quit();
    SDL_Quit();
    return 0;
}
