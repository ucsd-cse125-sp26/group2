#include "Game.hpp"

#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/systems/MovementSystem.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <cstring>

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

    // Spawn a local test entity to verify the physics tick is running.
    // Starts at y=200, not grounded — will fall each physics tick.
    const glm::vec3 k_startPos{0.0f, 200.0f, 0.0f};
    const entt::entity testEntity = registry.create();
    registry.emplace<Position>(testEntity, k_startPos);
    registry.emplace<PreviousPosition>(testEntity, k_startPos);
    registry.emplace<Velocity>(testEntity);
    registry.emplace<PlayerState>(testEntity);

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
        const char* k_msg = "Hello from client!";
        client.send(k_msg, static_cast<int>(strlen(k_msg)));
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

    // Guard against lag spikes stalling the physics for multiple seconds.
    frameTime = std::min(frameTime, 0.25f);
    accumulator += frameTime;

    // --- Fixed-step physics --------------------------------------------------
    while (accumulator >= k_physicsDt) {
        // Save current positions so the renderer can interpolate between them.
        registry.view<Position, PreviousPosition>().each(
            [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

        systems::runMovement(registry, k_physicsDt);
        accumulator -= k_physicsDt;
        ++tickCount;

        // Log once per second (every k_physicsHz ticks).
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
