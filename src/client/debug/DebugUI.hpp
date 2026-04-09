#pragma once

#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

/// @brief Live ECS inspector overlay powered by Dear ImGui.
///
/// **Ownership split:**
/// - DebugUI  — ImGui context, `imgui_impl_sdl3` input backend, UI state
/// - Renderer — `imgui_impl_sdlgpu3` render backend (owns the GPU device)
///
/// **Initialisation order in Game::init():**
/// 1. `debugUI.init(window)` — context must exist before GPU backend init
/// 2. `renderer.init(window)` — GPU backend init happens here
///
/// **Shutdown order in Game::quit():**
/// 1. `renderer.quit()` — GPU backend shutdown first
/// 2. `debugUI.shutdown()` — context destroyed last
class DebugUI
{
public:
    /// @brief Create the ImGui context and initialise the SDL3 input backend.
    /// @param window  The SDL window receiving input events.
    /// @return False if ImGui backend initialisation fails.
    bool init(SDL_Window* window);

    /// @brief Destroy the ImGui context and shut down the SDL3 input backend.
    void shutdown();

    /// @brief Forward an SDL event to the ImGui input backend.
    /// @param event  The event to process.
    void processEvent(const SDL_Event* event);

    /// @brief Begin a new ImGui frame. Call before any ImGui draw calls.
    void newFrame();

    /// @brief Build the ECS inspector window contents.
    /// @param registry   The ECS registry to inspect.
    /// @param tickCount  Total physics ticks elapsed (displayed in the stats bar).
    void buildUI(const Registry& registry, int tickCount);

    /// @brief Finalise the ImGui frame. Call after all ImGui draw calls, before Renderer::drawFrame().
    void render();

private:
    /// Per-component visibility toggles — persistent across frames.
    bool showPosition = true;       ///< Show Position component row.
    bool showPrevPosition = false;  ///< Show PreviousPosition component row.
    bool showVelocity = true;       ///< Show Velocity component row.
    bool showCollisionShape = true; ///< Show CollisionShape half-extents row.
    bool showPlayerState = true;    ///< Show PlayerState flags row.
    bool showInputSnapshot = true;  ///< Show InputSnapshot key-state row.
    bool showViewAngles = true;     ///< Show yaw/pitch/roll in degrees (easier to read than radians).
};
