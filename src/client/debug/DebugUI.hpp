#pragma once

#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

#include <glm/vec3.hpp>

class ParticleSystem; ///< Forward-declared to avoid pulling in heavy particle headers.

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

    /// @brief Build the Particle System debug/control window.
    /// @param ps       The particle system to inspect and control.
    /// @param eyePos   Camera eye position (used to compute spawn position).
    /// @param forward  Camera forward unit vector.
    void buildParticleUI(ParticleSystem& ps, glm::vec3 eyePos, glm::vec3 forward);

    /// @brief Finalise the ImGui frame. Call after all ImGui draw calls, before Renderer::drawFrame().
    void render();

private:
    // ── ECS Inspector state ────────────────────────────────────────────────
    bool showPosition = true;
    bool showPrevPosition = false;
    bool showVelocity = true;
    bool showCollisionShape = true;
    bool showPlayerState = true;
    bool showInputSnapshot = true;
    bool showViewAngles = true;
    bool showMovementChart = true;

    /// @brief Draw the standalone 2-D overhead movement chart window.
    void buildMovementChart(const Registry& registry);

    // ── Particle UI state ──────────────────────────────────────────────────
    float particleSpawnDist_ = 200.f; ///< Units ahead of camera to spawn effects.
    bool showParticleWindow_ = true;
};
