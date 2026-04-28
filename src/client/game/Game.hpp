/// @file Game.hpp
/// @brief Top-level client game class owning all subsystems and the main loop.

#pragma once

#include "animation/AnimationLibrary.hpp"
#include "animation/AnimationTesterUI.hpp"
#include "animation/CharacterRig.hpp"
#include "animation/SkinningBackend.hpp"
#include "debug/DebugUI.hpp"
#include "debug/FrameRecorder.hpp"
#include "ecs/components/ViewmodelConfig.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"
#include "network/MatchStatus.hpp"
#include "network/NetworkConfig.hpp"
#include "particles/ParticleSystem.hpp"
#include "sfx/SfxSystem.hpp"
#include "systems/KillFeedEvent.hpp"
#ifdef USE_HYBRID_RENDERER
#include "renderer/HybridRenderer.hpp"
#else
#include "renderer/Renderer.hpp"
#endif

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
    void refreshRemotePlayerRenderables();
    void refreshRemoteProjectileRenderables();

private:
    static constexpr int k_physicsHz = 128;                                      ///< Target physics tick rate.
    static constexpr float k_physicsDt = 1.0f / static_cast<float>(k_physicsHz); ///< Seconds per tick.
    static constexpr int k_maxTicksPerFrame = 8; ///< Spiral-of-death guard: max physics ticks per iterate().
    static constexpr int k_fpsHistorySize = 512; ///< Samples in the rolling FPS ring buffer.

    NetworkConfig netCfg;                        ///< Runtime network config loaded from config.toml.
    SDL_Window* window = nullptr;                ///< The application window.
    DebugUI debugUI;                             ///< Owns the ImGui context and SDL3 input backend.
#ifdef USE_HYBRID_RENDERER
    HybridRenderer renderer;                     ///< Routes each call to the legacy or new renderer.
#else
    Renderer renderer; ///< Legacy renderer.
#endif
    Registry registry;             ///< The shared ECS registry.
    Client client;                 ///< UDP network client.
    ParticleSystem particleSystem; ///< Client-side VFX particle system.
    SfxSystem sfxSystem;           ///< Client-side sound effects system.
    entt::dispatcher dispatcher;   ///< Event bus for weapon/impact/explosion events.

    Uint64 prevTime = 0;           ///< SDL performance counter at the last iterate() call.
    float accumulator = 0.0f;      ///< Unprocessed physics time in seconds.
    int tickCount = 0;             ///< Total physics ticks elapsed since start.
    bool mouseCaptured = true;     ///< True when relative mouse mode is active.

    // Runtime-tunable loop settings (exposed via ImGui)
    float mouseSensitivity = 0.001f;       ///< Radians per pixel of mouse movement.
    bool renderSeparateFromPhysics = true; ///< Render every iterate() with interpolation (true)
                                           ///  vs only after a physics tick (false).
    bool inputSyncedWithPhysics = true;    ///< Sample mouse once per physics tick (true)
                                           ///  vs every iterate() call (false).
    bool limitFPSToMonitor = true;         ///< VSync on (true) / off (false).

    Uint64 softLimitPeriod = 0;            ///< Target frame period in perf-counter ticks (0 = disabled).
    Uint64 softLimitNextFrame = 0;         ///< Performance counter target for next frame deadline.

    /// @brief Apply FPS-limit strategy based on limitFPSToMonitor, monitor Hz, and physics Hz.
    /// When monitor refresh >= physics Hz, uses VSync. Otherwise falls back to a
    /// software frame limiter at physics Hz with mailbox/immediate presentation.
    void applyFrameRateLimit();

    FrameRecorder recorder;  ///< R-key toggled frame-state + screenshot recorder.
    uint64_t frameCount = 0; ///< Monotonic render-frame counter.

    // Cached camera state — updated each iterate(), used by event() key shortcuts.
    glm::vec3 cachedEye_{0.f, 100.f, 0.f};
    glm::vec3 cachedCamFwd_{0.f, 0.f, 1.f};
    float currentCameraRoll_{0.0f}; ///< Smoothed camera roll angle (radians).

    // Model indices for entity rendering (loaded at init).
    int wraithModelIdx = -1;                       ///< Wraith player model index.
    int glowSphereModelIdx_ = -1;                  ///< Glow sphere for bloom testing (static).
    int movableSphereModelIdx_ = -1;               ///< Glow sphere that follows the player.
    int weaponModelIndices_[4] = {-1, -1, -1, -1}; ///< Per WeaponType, loaded at init.

    // Dynamic lighting test controls (ImGui-tunable)
    bool showDynLightUI_ = false;                        ///< Show the Dynamic Lighting panel.
    bool flashlightEnabled_ = false;                     ///< Point light at camera position.
    float flashlightIntensity_ = 8.0f;                   ///< Flashlight brightness.
    float flashlightRange_ = 800.0f;                     ///< Flashlight attenuation range.
    float flashlightOffset_ = 30.0f;                     ///< Forward offset from eye.
    bool movableSphereEnabled_ = false;                  ///< Glow sphere following the player.
    float sphereFollowDist_ = 150.0f;                    ///< Distance ahead of player.
    float sphereIntensity_ = 5.0f;                       ///< Point light intensity of movable sphere.
    float sphereRange_ = 500.0f;                         ///< Point light range of movable sphere.
    int glowCylinderModelIdx_ = -1;                      ///< Glow cylinder (beam) model index.
    bool beamEnabled_ = false;                           ///< Show the bloom beam.
    glm::vec3 beamStartOff_{30.0f, -5.0f, 10.0f};        ///< Beam start offset (fwd, up, right) from eye.
    glm::vec3 beamEndOff_{200.0f, -5.0f, 10.0f};         ///< Beam end offset (fwd, up, right) from eye.
    float beamRadius_ = 3.0f;                            ///< Beam cylinder radius.
    glm::vec3 beamColor_{0.6f, 0.1f, 1.0f};              ///< Beam emissive colour (purple).
    float beamLightIntensity_ = 4.0f;                    ///< Point light intensity per sample.
    float beamLightRange_ = 300.0f;                      ///< Point light range per sample.
    float beamLightSpacing_ = 60.0f;                     ///< Distance between point lights along beam.
    WeaponType currentEquippedType_ = WeaponType::Rifle; ///< Cached each frame.
    WeaponType lastEquippedType_ = WeaponType::Rifle; ///< Previous frame's weapon — triggers default reload on change.

    // Sound state tracking
    bool wasChargingRailgun_ = false; ///< True last frame if local player was charging RailGun.
    bool wasBeamActive_ = false;      ///< True last frame if local player's beam was active.

    // Hitmarker
    float hitmarkerTimer_ = 0.0f; ///< Remaining display time (fades out over this).

    // Viewmodel tuning (live-adjustable via ImGui)
    float vmScale = 0.03f;        ///< Weapon model scale (model is in mm).
    float vmForward = 21.0f;      ///< Forward offset from eye (Quake units).
    float vmRight = 5.5f;         ///< Right offset from eye.
    float vmDown = 22.5f;         ///< Downward offset from eye.
    float vmYawOffset = 58.0f;    ///< Extra yaw (degrees) applied to the model before camera orient.
    float vmPitchOffset = 12.0f;  ///< Extra pitch (degrees).
    float vmRollOffset = 2.0f;    ///< Extra roll (degrees).
    bool showViewmodelUI = false; ///< Show the Viewmodel Tweaker window.

    // Weapon sway state (CoD-style barrel lead)
    float prevSwayYaw_ = 0.0f;
    float prevSwayPitch_ = 0.0f;
    float swayOffsetX_ = 0.0f;     ///< Current horizontal sway (right axis, Quake units).
    float swayOffsetY_ = 0.0f;     ///< Current vertical sway (up axis).
    bool swayInitialized_ = false; ///< Guard against initial delta spike.

    // Sway tuning (ImGui-adjustable)
    float swayAmplitudeYaw_ = 3.0f;
    float swayAmplitudePitch_ = 2.0f;
    float swayDecayRate_ = 8.0f;  ///< Exponential decay speed.
    float swaySmoothing_ = 0.15f; ///< Input smoothing (0..1, lower = smoother).

    // Visual recoil state (viewmodel-only, does not affect aim)
    float recoilPitch_ = 0.0f;    ///< Current recoil pitch offset (degrees).
    float recoilPushBack_ = 0.0f; ///< Current recoil backward offset (Quake units).
    float recoilRoll_ = 0.0f;     ///< Current recoil roll offset (degrees).

    // Local weapon fire cooldown (mirrors server's per-weapon cooldown for VFX)
    float localFireCooldown_ = 0.0f; ///< Countdown timer; fire VFX only when <= 0.

    // Crosshair settings (ImGui-adjustable)
    float crosshairSize_ = 6.0f;                           ///< Half-length of each crosshair line (pixels).
    float crosshairThickness_ = 2.0f;                      ///< Line thickness (pixels).
    float crosshairGap_ = 3.0f;                            ///< Gap from center to start of each line (pixels).
    glm::vec4 crosshairColor_ = {0.0f, 1.0f, 0.0f, 0.85f}; ///< RGBA (default: green).
    bool crosshairDot_ = true;                             ///< Draw center dot.
    bool showCrosshair_ = true;                            ///< Master toggle.

    // Scroll-wheel weapon switching
    int pendingScrollSwitch_ = 0; ///< +1 = next slot, -1 = prev slot, consumed each frame.

    // Third-person weapon tuning (per weapon type, live-adjustable via ImGui)
    ThirdPersonWeaponParams tpWeaponParams_[4]; ///< Runtime-tunable copy; initialised from defaults.
    int tpTuneWeaponIdx_ = 0;                   ///< Which weapon type is being tuned.
    bool showTPWeaponUI_ = false;               ///< Show the 3P Weapon Tweaker window.

    // Animation subsystem — shared rig + clip library + skinning backend.
    // CharacterAnimators (one per animated entity) hold non-owning refs.
    CharacterRig charRig_;              ///< Shared skinned rig (skeleton + bind pose + weights).
    AnimationLibrary animLibrary_;      ///< Collection of ozz clips on the shared rig.
    CpuLbsSkinningBackend skinBackend_; ///< Phase-1 CPU linear-blend-skinning backend.
    AnimationTesterState animUI_;       ///< Persistent state for the Animation Tester panel.
    float kRigScale_ = 1.0f;            ///< Per-renderable scale for animated characters (auto-calculated, tunable).
    float kRigVerticalOffset_ =
        -90.0f;                ///< Per-renderable Y translation for animated characters (auto-calculated, tunable).
    float rigMeshMinY_ = 0.0f; ///< Minimum Y of the bind-pose mesh vertices (model space).

    // FPS ring buffer -- inter-render deltas, newest at (head-1) % size
    float fpsHistory[k_fpsHistorySize] = {}; ///< Circular buffer of per-frame FPS samples.
    int fpsHistoryHead = 0;                  ///< Next write index.
    int fpsHistoryCount = 0;                 ///< Valid sample count (saturates at k_fpsHistorySize).
    Uint64 prevRenderTime = 0;               ///< Perf counter at the last render call.

    // Network ping timer
    float pingTimer = 0.0f; ///< Accumulator for periodic PING sends.

    // Performance stats -- refreshed every 0.5 s
    Uint64 statsPrevTime = 0;       ///< Perf counter at the last stats snapshot.
    int statsPhysTicks = 0;         ///< Physics ticks accumulated since last snapshot.
    float measuredPhysicsHz = 0.0f; ///< Computed physics rate (Hz).
    float statsFPSCurrent = 0.0f;   ///< Most-recent render FPS sample.
    float statsFPSMin = 0.0f;       ///< Minimum FPS in the ring buffer.
    float statsFPSMax = 0.0f;       ///< Maximum FPS in the ring buffer.
    float statsFPS1pLow = 0.0f;     ///< 1st-percentile FPS (1 % low).
    float statsFPS5pLow = 0.0f;     ///< 5th-percentile FPS (5 % low).

    /// @brief Attach a fresh `AnimatedCharacter` component to an entity.
    ///
    /// Creates a new CharacterAnimator wired to the shared rig + clip library +
    /// skinning backend, uploads a per-entity clone of the rig's template model,
    /// and emplaces the component.  Safe to call even if the rig failed to load
    /// (logs a warning and leaves the entity un-animated).
    void attachAnimatedCharacter(entt::entity e);

    // Match State
    MatchPhase currentMatchPhase = MatchPhase::WARMUP; ///< Latest match phase update from the server.
    float countdownTimer = 0.0f; ///< Countdown timer for transitions between match phases (e.g. warmup to in-progress).

    // Kill Feed State
    std::vector<KillFeedEvent> killFeed; ///< Recent kill events for on-screen kill feed (newest first).
};
