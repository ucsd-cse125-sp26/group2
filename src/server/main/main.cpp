/// @file main.cpp
/// @brief Server application entry point.

#include "game/ServerGame.hpp"
#include "network/NetworkConfig.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

/// @brief Server entry point -- initialises SDL/NET, runs the game loop, and cleans up.
int main()
{
    SDL_Init(0);
    NET_Init();

    const char* base = SDL_GetBasePath();
    std::string cfgPath = std::string(base ? base : "") + "config.toml";
    const NetworkConfig cfg = loadNetworkConfig(cfgPath.c_str());
    const NetworkAddress& serverNet = cfg.serverNetwork;

    ServerGame game;
    // Phase 4a: pass the configured snapshot rate from config.toml
    // ([server-replication].snapshot-hz). Default 32 Hz; falls back to
    // compile-time default if missing.
    if (!game.init(serverNet.host.c_str(), serverNet.port, /*tickRateHz*/ 128, cfg.serverRep.snapshotHz)) {
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
