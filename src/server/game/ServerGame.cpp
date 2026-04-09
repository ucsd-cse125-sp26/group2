#include "ServerGame.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"

#include <SDL3/SDL.h>

// World geometry for the current test scene: a single floor plane at y=0.
// The normal (0,1,0) points upward into free space; distance=0 places it at the origin.
// Will be replaced by a proper World object when map loading is implemented.
static const std::array k_worldPlanes{physics::Plane{.normal = glm::vec3{0.0f, 1.0f, 0.0f}, .distance = 0.0f}};

bool ServerGame::init(const char* addr, Uint16 port, int hz)
{
    tickRateHz = hz;

    if (!server.init(addr, port))
        return false;

    // Spawn a test entity: starts at y=200, not grounded — will fall and land.
    const entt::entity k_testEntity = registry.create();
    registry.emplace<Position>(k_testEntity, glm::vec3{0.0f, 200.0f, 0.0f});
    registry.emplace<Velocity>(k_testEntity);
    registry.emplace<CollisionShape>(k_testEntity); // default: 32×72×32 standing AABB
    registry.emplace<PlayerState>(k_testEntity);

    SDL_Log("[server] spawned test entity at (0, 200, 0), tickRateHz=%d", tickRateHz);
    return true;
}

void ServerGame::run()
{
    running = true;

    const float k_dt = 1.0f / static_cast<float>(tickRateHz);
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_tickDuration = k_perfFreq / static_cast<Uint64>(tickRateHz);
    Uint64 nextTick = SDL_GetPerformanceCounter();

    while (running) {
        server.poll();
        tick(k_dt);

        nextTick += k_tickDuration;
        const Uint64 k_now = SDL_GetPerformanceCounter();
        if (k_now < nextTick) {
            const Sint64 k_sleepMs = static_cast<Sint64>((nextTick - k_now) * 1000 / k_perfFreq) - 1;
            if (k_sleepMs > 0)
                SDL_Delay(static_cast<Uint32>(k_sleepMs));

            // Spin-wait for the remaining sub-millisecond.
            while (SDL_GetPerformanceCounter() < nextTick) {
            }
        }
    }
}

void ServerGame::shutdown()
{
    running = false;
    server.shutdown();
}

void ServerGame::tick(float dt)
{
    systems::runMovement(registry, dt);
    systems::runCollision(registry, dt, k_worldPlanes);

    // Log once per second so we can watch the test entity fall and land.
    ++tickCount;
    if (tickCount % tickRateHz == 0) {
        registry.view<Position>().each([this](const Position& pos) {
            SDL_Log("[server] tick %d | pos (%.1f, %.1f, %.1f)",
                    tickCount,
                    static_cast<double>(pos.value.x),
                    static_cast<double>(pos.value.y),
                    static_cast<double>(pos.value.z));
        });
    }
}
