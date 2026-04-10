#pragma once

#include "debug/DebugUI.hpp"
#include "debug/FrameRecorder.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"
#include "particles/ParticleSystem.hpp"
#include "renderer/Renderer.hpp"

#include <SDL3/SDL.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

/// @brief Top-level client game object.
///
/// Owns all subsystems: window, ECS registry, renderer, debug UI, and network client.
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
    static constexpr int k_physicsHz = 128;                                      ///< Target physics tick rate.
    static constexpr float k_physicsDt = 1.0f / static_cast<float>(k_physicsHz); ///< Seconds per tick.
    static constexpr int k_maxTicksPerFrame = 8; ///< Spiral-of-death guard: max physics ticks per iterate().
    static constexpr int k_fpsHistorySize = 512; ///< Samples in the rolling FPS ring buffer.

    SDL_Window* window = nullptr;                ///< The application window.
    DebugUI debugUI;                             ///< Owns the ImGui context and SDL3 input backend.
    Renderer renderer;                           ///< Owns the GPU pipeline and ImGui render backend.
    Registry registry;                           ///< The shared ECS registry.
    Client client;                               ///< UDP network client.
    ParticleSystem particleSystem;               ///< Client-side VFX particle system.
    entt::dispatcher dispatcher;                 ///< Event bus for weapon/impact/explosion events.

    Uint64 prevTime = 0;                         ///< SDL performance counter at the last iterate() call.
    float accumulator = 0.0f;                    ///< Unprocessed physics time in seconds.
    int tickCount = 0;                           ///< Total physics ticks elapsed since start.
    bool mouseCaptured = true;                   ///< True when relative mouse mode is active.

    // ── runtime-tunable loop settings (exposed via ImGui) ────────────────────
    float mouseSensitivity = 0.001f;       ///< Radians per pixel of mouse movement.
    bool renderSeparateFromPhysics = true; ///< Render every iterate() with interpolation (true)
                                           ///  vs only after a physics tick (false).
    bool inputSyncedWithPhysics = true;    ///< Sample mouse once per physics tick (true)
                                           ///  vs every iterate() call (false).
    bool limitFPSToMonitor = false;        ///< VSync on (true) / off (false).

    FrameRecorder recorder;                ///< R-key toggled frame-state + screenshot recorder.
    uint64_t frameCount = 0;               ///< Monotonic render-frame counter.

    // Cached camera state — updated each iterate(), used by event() key shortcuts.
    glm::vec3 cachedEye_{0.f, 100.f, 0.f};
    glm::vec3 cachedCamFwd_{0.f, 0.f, 1.f};

    // Model indices for entity rendering (loaded at init).
    int wraithModelIdx = -1; ///< Wraith player model index.
    int weaponModelIdx = -1; ///< R-301 weapon model index.

    // ── FPS ring buffer — inter-render deltas, newest at (head-1) % size ─────
    float fpsHistory[k_fpsHistorySize] = {}; ///< Circular buffer of per-frame FPS samples.
    int fpsHistoryHead = 0;                  ///< Next write index.
    int fpsHistoryCount = 0;                 ///< Valid sample count (saturates at k_fpsHistorySize).
    Uint64 prevRenderTime = 0;               ///< Perf counter at the last render call.

    // ── Performance stats — refreshed every 0.5 s ───────────────────────────
    Uint64 statsPrevTime = 0;       ///< Perf counter at the last stats snapshot.
    int statsPhysTicks = 0;         ///< Physics ticks accumulated since last snapshot.
    float measuredPhysicsHz = 0.0f; ///< Computed physics rate (Hz).
    float statsFPSCurrent = 0.0f;   ///< Most-recent render FPS sample.
    float statsFPSMin = 0.0f;       ///< Minimum FPS in the ring buffer.
    float statsFPSMax = 0.0f;       ///< Maximum FPS in the ring buffer.
    float statsFPS1pLow = 0.0f;     ///< 1st-percentile FPS (1 % low).
    float statsFPS5pLow = 0.0f;     ///< 5th-percentile FPS (5 % low).
};
