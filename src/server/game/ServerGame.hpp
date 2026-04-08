#pragma once

#include "ecs/registry/Registry.hpp"
#include "network/Server.hpp"

#include <SDL3/SDL_stdinc.h>

// ---------------------------------------------------------------------------
// ServerGame — top-level server game loop.
//
// Owns the ECS registry and the network server.  Each tick it drains
// incoming datagrams, runs all ECS systems, and broadcasts state.
// ---------------------------------------------------------------------------

class ServerGame
{
public:
    // Bind to addr:port and set the tick period (milliseconds).
    bool init(const char* addr, Uint16 port, Uint32 tickRateMs);

    // Block and run the game loop until shutdown() is requested.
    void run();

    // Signal the loop to stop and release all resources.
    void shutdown();

private:
    void tick(float dt);

    Server server;
    Registry registry;
    bool running = false;
    Uint32 tickRateMs = 30;
};
