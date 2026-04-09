#pragma once

#include "debug/DebugUI.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"
#include "particles/ParticleSystem.hpp"
#include "renderer/Renderer.hpp"

#include <SDL3/SDL.h>

#include <entt/entt.hpp>

/// @brief Top-level client game object.
///
/// Owns all subsystems: window, ECS registry, renderer, debug UI, network client,
/// and particle system.
/// Wired into SDL's application-callback API (SDL_AppInit / SDL_AppEvent / SDL_AppIterate / SDL_AppQuit).
class Game
{
public:
    /// @brief Initialise all subsystems and spawn the local player entity.
    /// @return False on any fatal initialisation error.
    bool init();

    /// @brief Forward an SDL event to ImGui and handle application-level keys.
    /// @param event  The SDL event to process.
    /// @return SDL_APP_SUCCESS to quit, SDL_APP_CONTINUE to keep running.
    SDL_AppResult event(SDL_Event* event);

    /// @brief Advance one frame: sample input, step physics, render.
    /// @return SDL_APP_CONTINUE normally; SDL_APP_SUCCESS on quit request.
    SDL_AppResult iterate();

    /// @brief Shut down all subsystems in reverse-init order.
    void quit();

private:
    static constexpr int k_physicsHz =
        128; ///< Physics tick rate. The renderer interpolates between ticks using the accumulator.
    static constexpr float k_physicsDt = 1.0f / static_cast<float>(k_physicsHz); ///< Seconds per physics tick.

    SDL_Window* window = nullptr;                                                ///< The application window.
    DebugUI debugUI;               ///< Owns the ImGui context and SDL3 input backend.
    Renderer renderer;             ///< Owns the GPU pipeline and ImGui render backend.
    Registry registry;             ///< The shared ECS registry.
    Client client;                 ///< UDP network client.
    ParticleSystem particleSystem; ///< Client-side VFX particle system.
    entt::dispatcher dispatcher;   ///< Event bus for weapon/impact/explosion events.

    Uint64 prevTime = 0;           ///< SDL performance counter at the last iterate() call.
    float accumulator = 0.0f;      ///< Unprocessed physics time in seconds.
    int tickCount = 0;             ///< Total physics ticks elapsed since start.
    bool mouseCaptured = true;     ///< True when relative mouse mode is active.
};
