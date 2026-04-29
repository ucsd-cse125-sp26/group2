/// @file DebugUI.hpp
/// @brief Live ECS inspector overlay and debug windows powered by Dear ImGui.

#pragma once

#include "ecs/components/Hitbox.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/MatchStatus.hpp"

#include <SDL3/SDL.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <initializer_list>

struct NetworkStats;  ///< Forward-declared; defined in Client.hpp.
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
    /// @param registry                The ECS registry to inspect.
    /// @param tickCount               Total physics ticks elapsed.
    /// @param mouseSensitivity        Radians per pixel; slider (read/write).
    /// @param renderSeparateFromPhysics Interpolated unlimited-fps mode toggle (read/write).
    /// @param inputSyncedWithPhysics  Sample input once per tick vs every frame (read/write).
    /// @param limitFPSToMonitor       VSync on/off toggle (read/write).
    /// @param physicsHz               Measured physics tick rate (Hz).
    /// @param fpsCurrent              Most-recent render FPS sample.
    /// @param fpsMin                  Minimum FPS in the rolling window.
    /// @param fpsMax                  Maximum FPS in the rolling window.
    /// @param fps1pLow                1st-percentile FPS (1 % low).
    /// @param fps5pLow                5th-percentile FPS (5 % low).
    void buildUI(const Registry& registry,
                 int tickCount,
                 float& mouseSensitivity,
                 bool& renderSeparateFromPhysics,
                 bool& inputSyncedWithPhysics,
                 bool& limitFPSToMonitor,
                 int& ssrMode,
                 float physicsHz,
                 float fpsCurrent,
                 float fpsMin,
                 float fpsMax,
                 float fps1pLow,
                 float fps5pLow);

    /// @brief Build the Particle System debug/control window.
    /// @param ps       The particle system to inspect and control.
    /// @param eyePos   Camera eye position (used to compute spawn position).
    /// @param forward  Camera forward unit vector.
    void buildParticleUI(ParticleSystem& ps, glm::vec3 eyePos, glm::vec3 forward);

    /// @brief Build the Render Toggles window for live performance profiling.
    /// @param renderer  The renderer (for toggles, AA mode).
    void buildRenderTogglesUI(class Renderer& renderer);

    /// @brief Build the Skybox selector window for live HDR skybox swapping.
    void buildSkyboxUI(class Renderer& renderer);

    /// @brief Build the Lighting Controls window for live parameter tuning.
    void buildLightingUI(class Renderer& renderer);

    /// @brief Build the Network Stats window showing ping, bandwidth, and update rate.
    void buildNetworkUI(const NetworkStats& stats);
    void buildWeaponUI(const Registry& registry);

    /// @brief Draw hitbox capsule wireframes projected into screen space.
    ///
    /// Uses the ImGui foreground draw list to overlay world-space capsules on
    /// top of the rendered frame.  Toggled via `showHitboxes`.
    ///
    /// @param registry     ECS registry (reads HitboxInstance, Position, Player).
    /// @param viewProj     Combined view-projection matrix for the current camera.
    /// @param screenWidth  Viewport width in pixels.
    /// @param screenHeight Viewport height in pixels.
    void buildHitboxUI(const Registry& registry, const glm::mat4& viewProj, float screenWidth, float screenHeight);

    bool showHitboxes = false; ///< Show skeleton-driven hitbox capsule debug overlay.

    /// @brief Finalise the ImGui frame. Call after all ImGui draw calls, before Renderer::drawFrame().
    void render();

    /// @brief Toggle every debug panel on/off at once.
    ///
    /// If any panel is currently visible, all panels are hidden. If all panels are
    /// hidden, all panels become visible. Intended to be bound to a hotkey so the
    /// user can clear the overlay for clean gameplay/screenshots and bring it
    /// back with the same press.
    /// @param externalPanels  Visibility flags for panels owned outside DebugUI
    ///                        (e.g. the Animation Tester lives on Game). They
    ///                        participate in both the "any visible?" check and
    ///                        the bulk show/hide so the hotkey behavior stays
    ///                        consistent across all debug windows.
    void toggleAllPanels(std::initializer_list<bool*> externalPanels = {});

    bool pendingAmmoRefill_ = false; ///< Set by Weapon HUD button, consumed by Game::iterate().

private:
    /// Per-window visibility toggles — persistent across frames.
    bool showInspector = false;        ///< Show the main ECS Inspector window.
    bool showRenderToggles = false;    ///< Show the Render Toggles window.
    bool showLightingControls = false; ///< Show the Lighting Controls window.
    bool showSkybox = false;           ///< Show the Skybox window.
    bool showNetworkStats = false;     ///< Show the Network Stats window.
    // showHitboxes is public (declared above) to allow Game to pass it to toggleAllPanels.

    /// Per-component visibility toggles — persistent across frames.
    bool showPosition = true;       ///< Show Position component row.
    bool showPrevPosition = false;  ///< Show PreviousPosition component row.
    bool showVelocity = true;       ///< Show Velocity component row.
    bool showCollisionShape = true; ///< Show CollisionShape half-extents row.
    bool showPlayerState = true;    ///< Show PlayerState flags row.
    bool showInputSnapshot = true;  ///< Show InputSnapshot key-state row.
    bool showViewAngles = true;     ///< Show yaw/pitch/roll in degrees (easier to read than radians).
    bool showMovementChart = false; ///< Show the 2-D overhead movement chart window.
    bool showBhopAnalyzer = false;  ///< Show the bhop analyzer (player-relative + gain/sync).

    /// @brief Draw the body of the ECS Inspector window (everything after `ImGui::Begin`).
    ///
    /// Factored out of `buildUI` so the Begin/End wrapping (and early-out when
    /// the window is hidden) lives in one place in `buildUI`, while the content
    /// logic lives here. Parameter list mirrors `buildUI`.
    void buildInspectorContents(const Registry& registry,
                                int tickCount,
                                float& mouseSensitivity,
                                bool& renderSeparateFromPhysics,
                                bool& inputSyncedWithPhysics,
                                bool& limitFPSToMonitor,
                                int& ssrMode,
                                float physicsHz,
                                float fpsCurrent,
                                float fpsMin,
                                float fpsMax,
                                float fps1pLow,
                                float fps5pLow);

    /// @brief Draw the standalone 2-D overhead movement chart window.
    /// Shows the local player dot on a 3 000 × 3 000 unit grid together with
    /// view-direction, velocity, and wish-velocity arrows.
    void buildMovementChart(const Registry& registry);

    /// @brief Draw the bhop analyzer window.
    ///
    /// Shows velocity/wish vectors rotated into player-local space (player forward
    /// always points screen-up), plus numeric breakdowns, gain-per-frame, sync %,
    /// and history plots for horizontal speed and gain.
    void buildBhopAnalyzer(const Registry& registry);

    // Bhop analyzer rolling state (ring buffers, persist across frames).
    static constexpr int k_bhopHistorySize = 256;
    float bhopSpeedHistory_[k_bhopHistorySize] = {};
    float bhopGainHistory_[k_bhopHistorySize] = {};
    bool bhopAirborneHistory_[k_bhopHistorySize] = {};
    bool bhopGainingHistory_[k_bhopHistorySize] = {};
    int bhopHistoryIdx_ = 0;      ///< Next write slot in the ring buffers.
    int bhopHistoryFill_ = 0;     ///< Samples collected so far (up to k_bhopHistorySize).
    float bhopPrevHSpeed_ = 0.0f; ///< Previous frame's horizontal speed (for gain calc).
    bool bhopHasPrevSample_ = false;

    // Particle UI state
    float particleSpawnDist_ = 200.f; ///< Units ahead of camera to spawn effects.
    bool showParticleWindow_ = false;
};
