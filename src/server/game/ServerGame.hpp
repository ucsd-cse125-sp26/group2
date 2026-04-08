#pragma once

#include "ecs/registry/Registry.hpp"
#include "network/Server.hpp"

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// ServerGame — top-level server game loop.
//
// Owns the ECS registry and the network server. Each tick it drains
// incoming datagrams, runs all ECS systems, and broadcasts state.
// ---------------------------------------------------------------------------

class ServerGame
{
public:
    // Bind to addr:port. tickRateHz controls how many physics ticks run per
    // second — 128 is a good default for a LAN game.
    bool init(const char* addr, Uint16 port, int tickRateHz = 128);

    // Block and run the game loop until shutdown() is requested.
    void run();

    // Signal the loop to stop and release all resources.
    void shutdown();

private:
    void tick(float dt);

    Server server;
    Registry registry;
    bool running = false;
    int tickRateHz = 128;
    int tickCount = 0; // total ticks since start, used for periodic logging
};
