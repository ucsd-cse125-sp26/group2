#include "Game.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>

// World geometry for the current test scene: a single floor plane at y=0.
// Will be replaced by a proper World object when map loading is implemented.
static const std::array k_worldPlanes{physics::Plane{.normal = glm::vec3{0.0f, 1.0f, 0.0f}, .distance = 0.0f}};

bool Game::init()
{
    SDL_SetAppMetadata("group2", "0.1.0", "com.cse125.group2");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!NET_Init()) {
        SDL_Log("NET_Init() failed: %s", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow("group2", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    if (!renderer.init(window)) {
        SDL_Log("Renderer init failed");
        SDL_DestroyWindow(window);
        return false;
    }

    if (!client.init("127.0.0.1", 9999)) {
        SDL_Log("Failed to connect to server");
        SDL_DestroyWindow(window);
        return false;
    }

    // Spawn a local test entity: starts at y=200, not grounded — will fall and land.
    const glm::vec3 k_startPos{0.0f, 200.0f, 0.0f};
    const entt::entity k_testEntity = registry.create();
    registry.emplace<Position>(k_testEntity, k_startPos);
    registry.emplace<PreviousPosition>(k_testEntity, k_startPos);
    registry.emplace<Velocity>(k_testEntity);
    registry.emplace<CollisionShape>(k_testEntity); // default: 32x72x32 standing AABB
    registry.emplace<PlayerState>(k_testEntity);

    prevTime = SDL_GetPerformanceCounter();
    SDL_Log("[client] spawned test entity at (0, 200, 0), physicsHz=%d", k_physicsHz);
    return true;
}

SDL_AppResult Game::event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_SPACE) {
        // constexpr array decays to pointer — avoids k_msg const-pointer naming issue
        static constexpr char k_helloMsg[] = "Hello from client!";
        client.send(k_helloMsg, static_cast<int>(sizeof(k_helloMsg) - 1));
        SDL_Log("Sent message to server");
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult Game::iterate()
{
    // --- Compute frame time --------------------------------------------------
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_now = SDL_GetPerformanceCounter();

    float frameTime = static_cast<float>(k_now - prevTime) / static_cast<float>(k_perfFreq);
    prevTime = k_now;

    // Guard against lag spikes causing physics to spiral.
    frameTime = std::min(frameTime, 0.25f);
    accumulator += frameTime;

    // --- Fixed-step physics --------------------------------------------------
    while (accumulator >= k_physicsDt) {
        // Save positions so the renderer can interpolate between ticks.
        registry.view<Position, PreviousPosition>().each(
            [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

        systems::runMovement(registry, k_physicsDt);
        systems::runCollision(registry, k_physicsDt, k_worldPlanes);

        accumulator -= k_physicsDt;
        ++tickCount;

        // Log once per second.
        if (tickCount % k_physicsHz == 0) {
            registry.view<Position>().each([this](const Position& pos) {
                SDL_Log("[client] tick %d | pos (%.1f, %.1f, %.1f)",
                        tickCount,
                        static_cast<double>(pos.value.x),
                        static_cast<double>(pos.value.y),
                        static_cast<double>(pos.value.z));
            });
        }
    }

    // --- Network -------------------------------------------------------------
    while (client.poll()) {
    }

    // --- Render --------------------------------------------------------------
    renderer.drawFrame();
    return SDL_APP_CONTINUE;
}

void Game::quit()
{
    renderer.quit();
    client.shutdown();
    SDL_DestroyWindow(window);
    NET_Quit();
    SDL_Quit();
}
