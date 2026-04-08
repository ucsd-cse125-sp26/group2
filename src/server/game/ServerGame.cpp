#include "ServerGame.hpp"

#include "ecs/systems/Systems.hpp"

#include <SDL3/SDL.h>

bool ServerGame::init(const char* addr, Uint16 port, Uint32 tickMs)
{
    tickRateMs = tickMs;
    return server.init(addr, port);
}

void ServerGame::run()
{
    running = true;
    const float k_dt = static_cast<float>(tickRateMs) / 1000.0f;

    while (running) {
        server.poll();
        tick(k_dt);
        SDL_Delay(tickRateMs);
    }
}

void ServerGame::shutdown()
{
    running = false;
    server.shutdown();
}

void ServerGame::tick(float dt)
{
    systems::update(registry, dt);
}
