/// @file DebugUI.hpp
/// @brief Live ECS inspector overlay and debug windows powered by Dear ImGui.

#pragma once

#include "ecs/components/Hitbox.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/MatchStatus.hpp"
#include "network/ShotDebugReport.hpp" // PR-20: ShotDebugCapture.

// Forward-declared so DebugUI doesn't drag in raycast / hitbox headers via the
// gamepad aim-assist system.  Full definition pulled in by DebugUI.cpp.
namespace systems
{
struct GamepadAimAssistConfig;
}

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
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
    /// @param gamepadLookSensitivity  Radians per second at full right-stick deflection; slider (read/write).
    /// @param aimAssistCfg            Gamepad aim-assist config; sliders + toggle (read/write).
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
                 float& gamepadLookSensitivity,
                 systems::GamepadAimAssistConfig& aimAssistCfg,
                 bool& renderSeparateFromPhysics,
                 bool& inputSyncedWithPhysics,
                 bool& limitFPSToMonitor,
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

    /// @brief Build the Network Stats window showing ping, bandwidth, and update rate.
    void buildNetworkUI(const NetworkStats& stats);

    /// @brief Build the Network Simulator window with sliders to add
    /// fake round-trip latency and packet loss. Useful for testing
    /// Phase 6 lag compensation and Phase 5b reconciliation under
    /// non-LAN conditions without setting up `tc qdisc`.
    ///
    /// Latency slider value (0–200 ms) is written into
    /// `simulatedLatencyMs_` and read by Game::iterate which forwards
    /// it to `Client::setSimulatedLatencyMs`. Half the value is added
    /// to outbound packets and half to inbound, modelling a symmetric
    /// network with the slider's RTT.
    ///
    /// Packet-loss slider value (0–50 %) is written into
    /// `simulatedLossPct_`, forwarded to `Client::setSimulatedLossPercent`,
    /// and applied as an independent Bernoulli drop in each direction.
    void buildNetworkSimUI();

    /// @brief The current latency-simulator setting in milliseconds (0–200).
    /// Read by Game::iterate each frame to push to `Client::setSimulatedLatencyMs`.
    [[nodiscard]] int getSimulatedLatencyMs() const noexcept { return simulatedLatencyMs_; }

    /// @brief The current packet-loss setting (0–50 %).
    /// Read by Game::iterate each frame to push to `Client::setSimulatedLossPercent`.
    [[nodiscard]] int getSimulatedLossPercent() const noexcept { return simulatedLossPct_; }

    void buildWeaponUI(const Registry& registry);

    /// @brief Draw the Hitbox Debug window and (optionally) capsule wireframe overlay.
    ///
    /// The window is always rendered when `showHitboxWindow` is true.  The 3-D
    /// capsule overlay is drawn only when the "Draw Hitboxes" checkbox inside
    /// the window is checked.
    ///
    /// @param registry     ECS registry (reads HitboxInstance, Position, Player).
    /// @param viewProj     Combined view-projection matrix for the current camera.
    /// @param screenWidth  Viewport width in pixels.
    /// @param screenHeight Viewport height in pixels.
    void buildHitboxUI(const Registry& registry,
                       HitboxRig& hitboxRig,
                       const glm::mat4& viewProj,
                       float screenWidth,
                       float screenHeight);

    bool showHitboxWindow = false;  ///< Show the Hitbox Debug ImGui window.
    bool drawHitboxOverlay = false; ///< Draw 3-D capsule wireframes (independent of window visibility).

    /// @brief Draw the Collision Debug window and (optionally) wireframe overlay
    /// for all world collision primitives.
    ///
    /// @param world        The world geometry to visualise.
    /// @param viewProj     Combined view-projection matrix for the current camera.
    /// @param screenWidth  Viewport width in pixels.
    /// @param screenHeight Viewport height in pixels.
    void buildCollisionUI(const physics::WorldGeometry& world,
                          const glm::mat4& viewProj,
                          float screenWidth,
                          float screenHeight);

    bool showCollisionWindow = false;  ///< Show the Collision Debug ImGui window.
    bool drawCollisionOverlay = false; ///< Draw world collision wireframes (independent of window visibility).

    /// @brief Draw the Weapon Spawner Debug window and (optionally) wireframe overlay
    /// for all weapon spawner entities, showing their bounding boxes and spawn positions.
    ///
    /// @param registry     ECS registry (reads WeaponSpawner, Position, CollisionShape).
    /// @param viewProj     Combined view-projection matrix for the current camera.
    /// @param screenWidth  Viewport width in pixels.
    /// @param screenHeight Viewport height in pixels.
    void
    buildWeaponSpawnerUI(const Registry& registry, const glm::mat4& viewProj, float screenWidth, float screenHeight);

    bool showWeaponSpawnerWindow = false;  ///< Show the Weapon Spawner Debug ImGui window.
    bool drawWeaponSpawnerOverlay = false; ///< Draw weapon spawner wireframes (independent of window visibility).

    /// @brief Draw the Spawn Point Debug window and (optionally) overlay markers
    /// for all player respawn point entities, showing position and cooldown state.
    ///
    /// @param registry     ECS registry (reads RespawnPoint, Position).
    /// @param viewProj     Combined view-projection matrix for the current camera.
    /// @param screenWidth  Viewport width in pixels.
    /// @param screenHeight Viewport height in pixels.
    void buildSpawnPointUI(const Registry& registry, const glm::mat4& viewProj, float screenWidth, float screenHeight);

    bool showSpawnPointWindow = false;  ///< Show the Spawn Point Debug ImGui window.
    bool drawSpawnPointOverlay = false; ///< Draw spawn point markers (independent of window visibility).

    // ── PR-20: Shot-debug visualizer (CSGO sv_showimpacts-style) ─────
    //
    // Holds a ring buffer of paired (client-side fire-time snapshot,
    // server-side rewound snapshot) entries.  ImGui panel lets the
    // user pick how many recent shots to keep (1-30) and which to
    // highlight in the 3-D overlay.
    //
    // The two snapshots reuse the same `ShotDebugCapture` type:
    //   - clientView is populated by `pushClientShot()` from Game.cpp
    //     when the local player fires a shot (with the visible-on-
    //     screen capsules of remote players at fire time).
    //   - serverView is populated by `pushServerShot()` from the
    //     `Client::onShotDebugReport` callback (with the historical
    //     capsules the server raycast against).
    //
    // The two halves are paired by `shotInputTick`; either may
    // arrive first.
    struct ShotDebugPair
    {
        std::uint32_t shotInputTick = 0;
        bool hasClient = false;
        bool hasServer = false;
        net::shotdebug::ShotDebugCapture clientView;
        net::shotdebug::ShotDebugCapture serverView;
    };
    static constexpr int k_shotRingMax = 30;
    std::array<ShotDebugPair, k_shotRingMax> shotRing{};
    int shotRingHead = 0;  ///< Next-write slot.
    int shotRingCount = 0; ///< Live entries; saturates at k_shotRingMax.

    /// @brief Show the Shot Debug panel + 3D overlay (CSGO sv_showimpacts).
    bool showShotDebugWindow = false;
    bool drawShotDebugOverlay = false;
    /// @brief Slider value: how many of the most-recent shots to render
    /// (1-k_shotRingMax).  All older shots stay in the ring but are
    /// hidden from the overlay.
    int shotDebugVisibleCount = 5;
    /// @brief 1-based index into the ring (1 = newest) to highlight.
    /// 0 means "show all in the visible window".
    int shotDebugSelectIdx = 0;

    /// @brief Which side(s) to render in the 3D overlay.  Maps to a
    /// dropdown in the UI panel: 0=Both, 1=Client only, 2=Server only.
    /// The underlying data is always captured for both sides; this
    /// only filters the render.
    int shotDebugViewMode = 0;

    /// @brief Append a fire-time snapshot the local client just took.
    /// Pairs with any matching server view (by `shotInputTick`).
    void pushClientShot(const net::shotdebug::ShotDebugCapture& cap);

    /// @brief Append a server-reported rewind snapshot.  Pairs with
    /// any matching client view (by `shotInputTick`).
    void pushServerShot(const net::shotdebug::ShotDebugCapture& cap);

    /// @brief Build the Shot Debug ImGui window + 3D overlay.
    /// @param viewProj     Camera VP for the world→screen projection.
    /// @param screenWidth  Viewport width in pixels.
    /// @param screenHeight Viewport height in pixels.
    void buildShotDebugUI(const glm::mat4& viewProj, float screenWidth, float screenHeight);

    // Per-type visibility toggles (all default on when overlay is active).
    bool drawCollisionPlanes = true;
    bool drawCollisionBoxes = true;
    bool drawCollisionBrushes = true;
    bool drawCollisionCylinders = true;
    bool drawCollisionSpheres = true;
    bool drawCollisionTriMeshes = true;

    /// @brief Draw the Contact Debug window + (optionally) per-contact overlay.
    ///
    /// Records every physics contact (sweep hits + per-primitive depenetration
    /// MTV contributions) when the "Capture contacts" toggle is on, then
    /// renders them in the foreground draw list:
    ///   - small circle at each contact point (colour = source category)
    ///   - arrow along the contact normal (length = `contactNormalLength`,
    ///     scaled by depth for depenetration contacts)
    /// Per-source visibility toggles let you isolate (e.g.) just the
    /// trimesh-depen contributions — invaluable for debugging Phase 2
    /// internal-edge welding.
    ///
    /// `physics::debug::beginFrame()` MUST be called once per rendered frame
    /// before this — see Game::iterate.
    ///
    /// @param viewProj     Combined view-projection matrix for the current camera.
    /// @param screenWidth  Viewport width in pixels.
    /// @param screenHeight Viewport height in pixels.
    void buildContactDebugUI(const glm::mat4& viewProj, float screenWidth, float screenHeight);

    bool showContactDebugWindow = false; ///< Show the Contact Debug ImGui window.
    bool drawContactOverlay = false;     ///< Draw contact-point + normal overlay in 3D.

    // Per-source visibility toggles for the contact overlay.
    bool drawContactPlaneSweep = true;
    bool drawContactBoxSweep = true;
    bool drawContactBrushSweep = true;
    bool drawContactCylinderSweep = true;
    bool drawContactSphereSweep = true;
    bool drawContactTriMeshSweep = true;
    bool drawContactPlaneDepen = true;
    bool drawContactBoxDepen = true;
    bool drawContactBrushDepen = true;
    bool drawContactCylinderDepen = true;
    bool drawContactSphereDepen = true;
    bool drawContactTriMeshDepen = true;

    /// @brief World units of arrow length for unit-normal contacts.  Depen
    /// contacts scale this by their depth so deeper interpenetrations draw
    /// longer arrows.
    float contactNormalLength = 16.0f;

    /// @brief Radius (px) of the contact-point circle in screen space.
    float contactPointRadius = 4.0f;

    /// @brief Draw the per-trimesh welded-edge overlay: each triangle edge
    /// is coloured green when active (real boundary / convex corner) or red
    /// when welded (internal coplanar / concave seam suppressed at runtime).
    /// Validates the Phase 2 mesh-cook output by eye.
    bool drawMeshEdgeOverlay = false;

    /// @brief Optional vertex markers on welded-edge overlay (active = green dot).
    bool drawMeshVertexOverlay = false;

    /// @brief Finalise the ImGui frame. Call after all ImGui draw calls, before Renderer::drawFrame().
    void render();

    /// @brief Toggle the unified debug menu on/off.
    ///
    /// When visible, the debug menu shows one checkbox per debug panel. Individual
    /// panels are only drawn when their checkbox is checked.
    void toggleDebugMenu();

    /// @brief Build the unified debug menu window.
    ///
    /// Renders a single ImGui window with a checkbox per debug panel (both
    /// DebugUI-owned and external panels passed by Game). Only call when
    /// `showDebugMenu` is true.
    ///
    /// @param externalPanels  Named toggle pairs for panels owned outside DebugUI.
    struct ExternalPanel
    {
        const char* name; ///< Display name shown as checkbox label.
        bool* visible;    ///< Pointer to the visibility flag.
    };
    void buildDebugMenu(std::initializer_list<ExternalPanel> externalPanels);

    bool showDebugMenu = false; ///< Master toggle for the unified debug menu window.

    /// @brief Consume a start/stop request from the physics CSV button.
    ///
    /// The DebugUI toggles local client recording immediately; Game consumes
    /// this request to mirror the same state to the authoritative server.
    bool consumePhysicsCsvRecordingRequest(bool& enabled) noexcept;

    bool pendingAmmoRefill_ = false;             ///< Set by Weapon HUD button, consumed by Game::iterate().
    bool pendingAbilityLevelGrant_ = false;      ///< Set by Weapon HUD button, consumed by Game::iterate().
    std::int8_t pendingSetPrimaryWeapon_ = -1;   ///< -1 = none; else WeaponType ordinal. Consumed by Game::iterate().
    std::int8_t pendingSetSecondaryWeapon_ = -1; ///< Mirror for the secondary slot.

private:
    /// Per-window visibility toggles — persistent across frames.
    bool showInspector = false;    ///< Show the main ECS Inspector window.
    bool showNetworkStats = false; ///< Show the Network Stats window.
    bool showNetworkSim = false;   ///< Show the Network Simulator (latency + loss) window.
    bool showWeaponHud = false;    ///< Show the Weapon HUD debug window (disabled by default).

    bool pendingPhysicsCsvRecordingRequest_ = false;
    bool pendingPhysicsCsvRecordingEnabled_ = false;

    /// @brief Phase 6 testing: simulated round-trip latency in ms.
    /// Written by the Network Simulator window's slider, read by
    /// Game::iterate to push to Client. Zero = simulator off.
    int simulatedLatencyMs_ = 0;

    /// @brief Phase 6 testing: simulated UDP packet loss in percent.
    /// Applied independently to each direction. Zero = simulator off.
    int simulatedLossPct_ = 0;

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
                                float& gamepadLookSensitivity,
                                systems::GamepadAimAssistConfig& aimAssistCfg,
                                bool& renderSeparateFromPhysics,
                                bool& inputSyncedWithPhysics,
                                bool& limitFPSToMonitor,
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
