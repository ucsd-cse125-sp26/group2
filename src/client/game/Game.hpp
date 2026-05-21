/// @file Game.hpp
/// @brief Top-level client game class owning all subsystems and the main loop.

#pragma once

#include "IScreen.hpp"
#include "animation/AnimationLibrary.hpp"
#include "animation/AnimationTesterUI.hpp"
#include "animation/CharacterRig.hpp"
#include "animation/SkinningBackend.hpp"
#include "app/AppContext.hpp"
#include "debug/DebugUI.hpp"
#include "debug/FrameRecorder.hpp"
#include "ecs/AssetRegistry.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/ViewmodelConfig.hpp"
#include "ecs/physics/MapLoader.hpp"
#include "ecs/registry/Registry.hpp"
#include "hud/Hud.hpp"
#include "menus/pause/PauseMenu.hpp"
#include "network/Client.hpp"
#include "network/MatchStatus.hpp"
#include "network/RegistrySerialization.hpp"
#include "particles/ParticleSystem.hpp"
#include "renderer-new/NewRenderer.hpp"
#include "sfx/SfxSystem.hpp"
#include "systems/GamepadAimAssistSystem.hpp"
#include "systems/InputRingBuffer.hpp"
#include "systems/KillFeedEvent.hpp"
#include "util/WorkerPool.hpp"
#include "voice/VoiceChatSystem.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <deque>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/// @brief Top-level client game object.
///
/// Owns the active game screen subsystems and borrows App-owned window, renderer, and network client.
/// Wired through App into SDL's application-callback API.
class Game : public IScreen
{
public:
    /// @brief Create the Game-owned ImGui context before App initialises the renderer backend.
    bool initDebugUI(const AppContext& ctx);

    /// @brief Initialise all subsystems and spawn the local player entity.
    /// @return False on any fatal initialisation error.
    bool init(AppContext& ctx);

    /// @brief Forward an SDL event to ImGui and handle application-level keys.
    /// @param event  The SDL event to process.
    /// @return SDL_APP_SUCCESS to quit, SDL_APP_CONTINUE to keep running.
    SDL_AppResult event(SDL_Event* event) override;

    /// @brief Advance one frame: sample input, step physics, render.
    ///
    /// Execution flow each frame:
    ///  1. **Time accumulation** — compute frame delta, detect suspend/background gaps,
    ///     clamp to avoid spiral-of-death.
    ///  2. **Performance stats** — refresh FPS percentiles and physics Hz every 0.5 s.
    ///  3. **Input sampling** — mouse look runs every frame for smooth camera;
    ///     movement keys run once per physics tick group when inputSyncedWithPhysics
    ///     is true; weapon keys sampled every frame.
    ///  4. **Network** — send input to server, send periodic pings, poll bandwidth.
    ///  5. **Physics** — drain accumulator at 128 Hz (up to k_maxTicksPerFrame),
    ///     snapshot PreviousPosition, poll network for state updates, refresh
    ///     remote renderables.
    ///  6. **Camera resolve** — interpolate local player position between ticks
    ///     (or use post-tick position in sequential mode).
    ///  7. **Local weapon VFX** — fire cooldown, spawn tracers/impacts,
    ///     visual recoil kick (viewmodel-only, does not affect aim).
    ///  8. **Subsystem updates** — flush dispatcher events, update particles,
    ///     SFX, skeletal animation (CPU skinning + hitbox capsules).
    ///  9. **Entity render list** — build world-space transforms for entities,
    ///     third-person weapons, glow spheres, beam visuals, point lights.
    /// 10. **Viewmodel** — weapon sway, bob, recoil decay, camera-space transform.
    /// 11. **Frame recording** — if R-key recording is active, capture frame state.
    /// 12. **FPS ring buffer** — record inter-render delta for stats.
    /// 13. **Debug UI** — ImGui panels (debug menu, network, particles, hitbox,
    ///     lighting, viewmodel tweaker, 3P weapon tweaker, scoreboard).
    /// 14. **HUD** — gather game state, update and render the HUD overlay.
    /// 15. **Render** — drawFrame with interpolated camera, apply VSync changes.
    /// 16. **Software frame limiter** — sleep + spin-wait if targeting above
    ///     monitor refresh rate.
    ///
    /// @return SDL_APP_CONTINUE normally; SDL_APP_SUCCESS on quit request.
    /// @see ServerGame::tick for the authoritative server-side equivalent.
    SDL_AppResult iterate() override;

    /// @brief True once the server has returned the match phase to the lobby.
    bool shouldReturnToLobby() const;

    /// @brief True if the user requested leaving the match for the main menu, then clear that request.
    bool consumeReturnToMainMenu();

    /// @brief Shut down all subsystems in reverse-init order.
    void quit() override;

    /// @brief Destroy the Game-owned ImGui context after App shuts down the renderer backend.
    void shutdownAfterRenderer() override;

    /// @brief Update Renderable components for remote players (model, scale, orientation from animation rig).
    void refreshRemotePlayerRenderables();

    /// @brief Assign Renderable components to newly spawned projectile entities.
    void refreshRemoteProjectileRenderables();

    /// @brief Reset Renderable visibility for players transitioning through respawn.
    void refreshRemoteRespawnRenderables();
    void refreshRemotePowerupRenderables();

    /// @brief Assign Renderable components to dropped-weapon entities (mirrors spawner visuals).
    void refreshDroppedWeaponRenderables();

private:
    /// @brief Deserialize a snapshot from the server and update the ECS registry.
    /// @return True on success; false if the snapshot could not be applied.
    bool applyIncomingSnapshot(
        std::uint32_t snapshotTick, const std::uint8_t* bytes, Uint32 size, Uint64 captureNs, std::uint32_t& ackedTick);
    /// @brief Emplace player-control components onto the mapped local entity and record it.
    void handleLocalPlayerReady(entt::entity local);
    void openChat();
    void closeChat();
    void submitChat();
    void appendChatMessage(ClientId sender, std::string_view message);
    void appendLocalChatMessage(std::string_view message);
    void clearGameplayInputForChat();

    static constexpr int k_physicsHz = 128;                                      ///< Target physics tick rate.
    static constexpr float k_physicsDt = 1.0f / static_cast<float>(k_physicsHz); ///< Seconds per tick.
    /// Spiral-of-death guard: max physics ticks per iterate().  Dropped from
    /// 8 to 2 in Phase 3g — the bench profiler showed the slowest frames are
    /// dominated by catch-up bursts (e.g. 8 ticks × ~2 ms = 16 ms in one
    /// frame, dragging p1/p5).  Capping at 2 spreads the catch-up across
    /// more render frames so any individual frame's worst case is ~4 ms
    /// instead of ~16 ms.  Visual consequence on a stall: physics simulation
    /// briefly runs 0.5× wall speed until the accumulator drains naturally;
    /// human-imperceptible at 1000+ render Hz.
    static constexpr int k_maxTicksPerFrame = 2;
    static constexpr int k_fpsHistorySize = 512;                   ///< Samples in the rolling FPS ring buffer.

    SDL_Window* window = nullptr;                                  ///< The application window.
    DebugUI debugUI;                                               ///< Owns the ImGui context and SDL3 input backend.
    NewRenderer* renderer = nullptr;                               ///< Borrowed renderer owned by App.
    Registry registry;                                             ///< The shared ECS registry.
    Client* client = nullptr;                                      ///< Borrowed UDP network client owned by App.
    UserSettings* userSettings = nullptr;                          ///< Borrowed user settings owned by App.
    std::string_view userSettingsPath_;                            ///< Borrowed save path for user settings.
    std::optional<registry_serialization::Loader> snapshotLoader_; ///< Incremental loader; created on first snapshot.
    std::optional<entt::entity>
        mappedLocalPlayerEntity_;  ///< Local-registry entity for this client's player, once assigned.
    ParticleSystem particleSystem; ///< Client-side VFX particle system.
    SfxSystem sfxSystem;           ///< Client-side sound effects system.
    VoiceChatSystem voiceChat_;    ///< Push-to-talk Opus proximity voice chat.
    Hud hud_;                      ///< In-game HUD overlay system.
    entt::dispatcher dispatcher;   ///< Event bus for weapon/impact/explosion events.

    Uint64 prevTime = 0;           ///< SDL performance counter at the last iterate() call.
    float accumulator = 0.0f;      ///< Unprocessed physics time in seconds.
    int tickCount = 0;             ///< Total physics ticks elapsed since start.
    /// @brief Monotonic per-tick counter stamped onto outgoing InputSnapshots.
    ///
    /// Bumped once per physics tick group inside iterate() and copied into the
    /// local player's InputSnapshot.tick before each send. The server uses
    /// this to dedup re-sent inputs (multi-input redundancy) and apply only
    /// inputs newer than its lastAppliedInputTick. Phase-5 prediction also
    /// keys the input ring buffer by it.
    uint32_t clientPredictTick = 0;

    /// @brief PR-20: tracks last frame's `input.shooting` so the
    /// fire-rising-edge detector inside `iterate()` only captures
    /// the FIRST tick of a click (a "trigger pull"), not every tick
    /// the button is held.  Survives across frames as a member; is
    /// naturally reset when the local player respawns because the
    /// View<LocalPlayer> branch returns early during the dead-window
    /// (no InputSnapshot present).  Implementation detail of the
    /// shot-debug visualizer.
    bool prevShootingForDebug_ = false;

    /// @brief Phase 5b: ring buffer of recent stamped inputs for replay-
    /// based reconciliation. Each entry is keyed by clientPredictTick so
    /// runReconciliation can look up the input that was sent for any
    /// recent tick and feed it back into runMovement during replay.
    InputRingBuffer inputRing_;
    bool mouseCaptured = true; ///< True when relative mouse mode is active.

    /// @brief Currently-bound gamepad, or nullptr if none is plugged in.
    ///
    /// Opened on SDL_EVENT_GAMEPAD_ADDED (first device wins — extra controllers
    /// are ignored until the active one disconnects), closed on
    /// SDL_EVENT_GAMEPAD_REMOVED.  SDL3's gamepad mapping database normalises
    /// every supported controller (Xbox 360 / One, DualShock, Switch Pro, ...)
    /// onto the same logical buttons + axes, so the input mapping in
    /// InputSampleSystem.hpp works uniformly across devices.
    SDL_Gamepad* activeGamepad_ = nullptr;
    /// @brief SDL_JoystickID of the active gamepad — needed to identify the
    /// device on SDL_EVENT_GAMEPAD_REMOVED so we don't tear down a different
    /// controller when a second one disconnects.
    SDL_JoystickID activeGamepadId_ = 0;
    /// @brief Right-stick look speed in radians per second at full deflection.
    /// 6.0 rad/s ≈ 343°/s — most testers found 3.0 too sluggish for tracking
    /// players during firefights; this is in line with mainstream console FPS
    /// defaults.  Tunable from the ECS inspector.
    float gamepadLookSensitivity = 6.0f;

    /// @brief AAA-style gamepad aim assist tuning.
    ///
    /// Active only when a gamepad is connected (mouse input is unaffected).
    /// Defaults are tuned for *assist* not *auto-aim*: a stationary enemy
    /// gets zero rotational pull, a moving enemy gets partial tracking
    /// help.  Live-tunable from the ECS inspector.
    systems::GamepadAimAssistConfig aimAssistCfg_;

    /// @brief Persistent inter-frame state for aim assist (anchor on target
    /// AABB + previous-frame snapshot used to compute the angular delta
    /// from which the rotational pull is derived).  Reset implicitly when
    /// the target is lost or aim assist is disabled.
    systems::GamepadAimAssistState aimAssistState_;

    /// Persistent thread pool for parallel-for over per-frame loops
    /// (currently the animation update; future: parallel frustum cull,
    /// particle update, ECS transforms).  Initialised in Game::init with a
    /// worker count derived from `std::thread::hardware_concurrency() / 2`
    /// (or `GROUP2_WORKERS` if set), so we leave half the cores for the
    /// rest of the system.  Gated behind a unique_ptr so it constructs
    /// AFTER `init()` reads the env override.
    std::unique_ptr<WorkerPool> workerPool_;

    // Runtime-tunable loop settings (exposed via ImGui)
    float mouseSensitivity = 0.0007f;      ///< Radians per pixel of mouse movement.
    float horizontalFovDegrees = 90.0f;    ///< Player-facing horizontal camera field of view in degrees.
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
    bool cachedGravFlipped_{false}; ///< Local player gravity-flip state, updated each iterate().
    float currentCameraRoll_{0.0f}; ///< Smoothed camera roll angle (radians).

    // Map collision data — loaded from GLB, owns the vectors that back activeWorld().
    physics::MapCollisionData mapCollision_;

    // Central asset registry — maps human-readable names to renderer model indices.
    AssetRegistry assets_;

    // Legacy model index aliases (for code that still uses raw indices).
    // TODO: migrate all call sites to assets_.modelIndex("name") and remove these.
    int weaponModelIndices_[4] = {-1, -1, -1, -1};
    int weaponAssetIds_[4] = {-1, -1, -1, -1};

    int rocketProjectileModelIdx_ = -1;

    // Dynamic lighting test controls (ImGui-tunable)
    bool showDynLightUI_ = false;                        ///< Show the Dynamic Lighting panel.
    bool showHudDebug_ = false;                          ///< Show the HUD Tweaker panel.
    bool flashlightEnabled_ = false;                     ///< Point light at camera position.
    float flashlightIntensity_ = 8.0f;                   ///< Flashlight brightness.
    float flashlightRange_ = 800.0f;                     ///< Flashlight attenuation range.
    float flashlightOffset_ = 30.0f;                     ///< Forward offset from eye.
    bool movableSphereEnabled_ = false;                  ///< Glow sphere following the player.
    float sphereFollowDist_ = 150.0f;                    ///< Distance ahead of player.
    float sphereIntensity_ = 5.0f;                       ///< Point light intensity of movable sphere.
    float sphereRange_ = 500.0f;                         ///< Point light range of movable sphere.
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
    bool viewmodelDefaultsApplied_ = false;

    // Sound state tracking
    bool wasChargingRailgun_ = false; ///< True last frame if local player was charging RailGun.
    bool wasBeamActive_ = false;      ///< True last frame if local player's beam was active.
    SfxSystem::SourceHandle beamLoopHandle_ = SfxSystem::kInvalidSource;
    std::unordered_map<entt::entity, std::array<float, 5>> footstepPhases_;
    std::unordered_map<entt::entity, float> footstepCooldowns_;

    // Hitmarker
    float hitmarkerTimer_ = 0.0f;       ///< Remaining display time (fades out over this).
    bool hitmarkerIsHeadshot_ = false;  ///< True when the current hitmarker was a headshot.
    bool hitmarkerShieldBreak_ = false; ///< True when the current hit depleted target armor.

    // Floating damage numbers — queued from replicated particle events, consumed by HUD each frame.
    struct PendingDamageNumber
    {
        glm::vec3 pos;
        float damage;
        bool headshot;
        bool shielded;
    };
    std::vector<PendingDamageNumber> pendingDamageNumbers_;

    // Damage accumulator — tracks continuous damage to a single target.
    entt::entity accumTarget_ = entt::null; ///< Current target being damaged.
    int accumTotal_ = 0;                    ///< Running damage total.
    float accumResetTimer_ = 0.f;           ///< Timer to reset accumulator after inactivity.
    uint8_t accumLastHitType_ = 0;          ///< 0=health(white), 1=shield(blue), 2=headshot(gold).

    // Vignette state: track previous frame health/armor for delta detection.
    float prevHealth_ = 100.f;
    float prevArmor_ = 100.f;

    // ── Voidfall HUD bookkeeping ────────────────────────────────────────
    /// @brief Time accumulated while the match is in PLAYING phase (s).
    /// Drives the top-center match-header readout.  Reset on phase changes
    /// other than PLAYING so a fresh match starts at 0:00.
    float matchElapsedSeconds_ = 0.f;

    /// @brief Last-frame snapshot of the local player's weapon-slot types
    /// (-1 = empty).  Used to detect new weapons appearing → emit a pickup
    /// notification ("+1 RIFLE") so the side feed reflects what just changed.
    int prevPrimaryWeaponType_ = -1;
    int prevSecondaryWeaponType_ = -1;
    int prevAmmoReserve_ = -1; ///< Drives "+N <weapon> AMMO" reserve-grow notifications.

    /// @brief Pickup notifications queued for the next HUD frame.  Emitted
    /// when the local player's WeaponState gains a new weapon type or their
    /// reserve ammo grows beyond the previous frame's reading.
    std::vector<HudPickupNotification> pendingPickupNotifications_;
    std::vector<HudChatMessage> chatMessages_;
    std::vector<HudVoiceSpeaker> voiceSpeakers_;
    std::string chatDraft_;
    std::deque<std::string> pendingLocalChatEchoes_;
    bool chatOpen_ = false;

    // (No additional bookkeeping needed — name strings are constructed
    // each frame into the thread_local vector inside Game.cpp.)

    // Viewmodel tuning (live-adjustable via ImGui)
    float vmScale = 1.0f;         ///< Weapon model scale (model is in mm).
    float vmForward = 0.0f;       ///< Forward offset from eye (Quake units).
    float vmRight = 0.0f;         ///< Right offset from eye.
    float vmDown = 0.0f;          ///< Downward offset from eye.
    float vmYawOffset = 0.0f;     ///< Extra yaw (degrees) applied to the model before camera orient.
    float vmPitchOffset = 0.0f;   ///< Extra pitch (degrees).
    float vmRollOffset = 0.0f;    ///< Extra roll (degrees).
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

    int pendingScrollSwitch_ = 0;    ///< +1 = next slot, -1 = previous slot, consumed each frame.

    // Third-person weapon tuning (per weapon type, live-adjustable via ImGui)
    ThirdPersonWeaponParams tpWeaponParams_[4]; ///< Runtime-tunable copy; initialised from defaults.
    int tpTuneWeaponIdx_ = 0;                   ///< Which weapon type is being tuned.
    bool showTPWeaponUI_ = false;               ///< Show the 3P Weapon Tweaker window.

    // Weapon spawner model tuning (per weapon type, live-adjustable via ImGui)
    WeaponSpawnerModelParams spawnerWeaponParams_[4]; ///< Runtime-tunable copy; initialised from spawner defaults.
    int spawnerTuneWeaponIdx_ = 0;                    ///< Which weapon type is being tuned.
    bool showWeaponSpawnerModelUI_ = false;           ///< Show the Weapon Spawner Model Tweaker window.

    // Animation subsystem — shared rig + clip library + skinning backend.
    // CharacterAnimators (one per animated entity) hold non-owning refs.
    CharacterRig charRig_;              ///< Shared skinned rig (skeleton + bind pose + weights).
    AnimationLibrary animLibrary_;      ///< Collection of ozz clips on the shared rig.
    CpuLbsSkinningBackend skinBackend_; ///< Phase-1 CPU linear-blend-skinning backend.
    AnimationTesterState animUI_;       ///< Persistent state for the Animation Tester panel.
    HitboxRig clientHitboxRig_;         ///< Hitbox definitions for client-side debug visualization.
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

    // Benchmark mode: when BENCH_SECONDS env var is set to a positive number,
    // the client runs for that many seconds, prints a one-line FPS summary to
    // stderr, then quits.  Powers `scripts/perf-100bots.sh`.
    float benchSeconds_ = 0.0f;                         ///< Bench duration in seconds (0 = disabled).
    Uint64 benchStartTime_ = 0;                         ///< Perf counter at first iterate() in bench mode.
    bool benchActive_ = false;                          ///< True after BENCH_SECONDS read at init.
    static constexpr float k_benchWarmupSeconds = 2.0f; ///< Skip the first N seconds (pipeline warmup).
    std::vector<float> benchFrameTimesMs_;              ///< Per-frame ms after warmup; reservation in init().

    /// Per-frame phase-time breakdown.  Captured every frame in bench mode,
    /// then the slowest 10 frames' breakdowns are dumped at exit so we can
    /// attribute p1/p5 spikes to specific subsystems (e.g. "the spike was a
    /// drawFrame stall waiting for the swapchain" vs "an animation-update
    /// burst hit several chars at once").
    struct FrameSectionMs
    {
        float total = 0.0f;
        float input = 0.0f;
        float networkPoll = 0.0f;
        float physics = 0.0f;
        float animation = 0.0f;        ///< ozz pose + skin matrix + hitbox.
        float skinPaletteBuild = 0.0f; ///< Frustum cull + palette + instance build.
        float entityCmds = 0.0f;       ///< Renderable + 3p weapon list build.
        float particles = 0.0f;
        float imgui = 0.0f;
        float drawFrame = 0.0f; ///< Renderer drawFrame (CPU + GPU acquire).
        // Sub-breakdown of drawFrame, populated from Renderer::lastAcquire/Record/SubmitMs.
        // The "acquire" ms specifically captures swapchain back-pressure stalls — when
        // the GPU hasn't released a previous swap image, the CPU thread blocks here.
        float drawAcquire = 0.0f;
        float drawRecord = 0.0f;
        float drawSubmit = 0.0f;
    };
    std::vector<FrameSectionMs> benchFrameStats_;

    /// @brief Attach a fresh `AnimatedCharacter` component to an entity.
    ///
    /// Creates a new CharacterAnimator wired to the shared rig + clip library +
    /// skinning backend, uploads a per-entity clone of the rig's template model,
    /// and emplaces the component.  Safe to call even if the rig failed to load
    /// (logs a warning and leaves the entity un-animated).
    void attachAnimatedCharacter(entt::entity e);

    // Match State
    MatchPhase currentMatchPhase = MatchPhase::LOBBY; ///< Latest match phase update from the server.
    float countdownTimer = 0.0f; ///< Countdown timer for transitions between match phases (e.g. warmup to in-progress).
    bool returnToLobbyRequested = false;     ///< Latched true when server sends MATCH_STATE with phase == LOBBY.
    bool returnToMainMenuRequested_ = false; ///< Latched true when the pause menu requests leaving the match.

    // Kill Feed State
    std::vector<KillFeedEvent> killFeed; ///< Recent kill events for on-screen kill feed (newest first).

    PauseMenu pauseMenu; ///< In-game pause menu (opened with ESC, blocks input to the game when active).
};
