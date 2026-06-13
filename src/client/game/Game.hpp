/// @file Game.hpp
/// @brief Top-level client game class owning all subsystems and the main loop.

#pragma once

#include "IScreen.hpp"
#include "animation/AnimationLibrary.hpp"
#include "animation/AnimationTesterUI.hpp"
#include "animation/CharacterRig.hpp"
#include "animation/SkinningBackend.hpp"
#include "app/AppContext.hpp"
#include "debug/ClientPerfRecorder.hpp"
#include "debug/DebugUI.hpp"
#include "debug/FrameRecorder.hpp"
#include "ecs/AssetCatalog.hpp"
#include "ecs/AssetRegistry.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerSimState.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/ViewmodelConfig.hpp"
#include "ecs/physics/MapLoader.hpp"
#include "ecs/registry/Registry.hpp"
#include "hud/Hud.hpp"
#include "menus/pause/PauseMenu.hpp"
#include "menus/postmatch/PostMatchResult.hpp"
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
#include <filesystem>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

    /// @brief Return the final scoreboard snapshot captured before the server reset match stats.
    std::optional<PostMatchResult> consumePostMatchResult();

    /// @brief True if the user requested leaving the match for the main menu, then clear that request.
    bool consumeReturnToMainMenu();

    /// @brief True if the main-menu return was caused by the server connection closing, then clear that reason.
    bool consumeServerShutdownNotice();

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

    /// @brief Assign or update Renderable components for replicated powerup entities.
    void refreshRemotePowerupRenderables();

    /// @brief Assign or update Renderable components for replicated health pack spawner entities.
    void refreshRemoteHealthPackRenderables();

    /// @brief Assign Renderable components to dropped-weapon entities (mirrors spawner visuals).
    void refreshDroppedWeaponRenderables();

private:
    /// @brief Deserialize a snapshot from the server and update the ECS registry.
    /// @return True on success; false if the snapshot could not be applied.
    bool applyIncomingSnapshot(
        std::uint32_t snapshotTick, const std::uint8_t* bytes, Uint32 size, Uint64 captureNs, std::uint32_t& ackedTick);
    /// @brief Emplace player-control components onto the mapped local entity and record it.
    void handleLocalPlayerReady(entt::entity local);

    struct PredictedPlayerState
    {
        std::uint32_t tick = 0;
        Position position{};
        PreviousPosition previousPosition{};
        Velocity velocity{};
        PlayerVisState vis{};
        PlayerSimState sim{};
        InputSnapshot input{};
        bool valid = false;
    };

    struct ReconciliationDecision
    {
        bool skip = false;
        bool missingHistory = false;
        float positionError = 0.0f;
        float velocityError = 0.0f;
    };

    void clearPredictedStateHistory() noexcept;
    void storePredictedPlayerState(std::uint32_t tick);
    [[nodiscard]] std::optional<PredictedPlayerState> captureLocalPredictedState() const;
    [[nodiscard]] const PredictedPlayerState* predictedStateForTick(std::uint32_t tick) const noexcept;
    void restoreLocalPredictedState(const PredictedPlayerState& state);
    [[nodiscard]] ReconciliationDecision
    evaluateReconciliationSkip(const PredictedPlayerState& authoritative,
                               const PredictedPlayerState* predictedAtAck,
                               const std::optional<PredictedPlayerState>& currentBeforeSnapshot) const noexcept;

    /// @brief Refresh the final post-match scoreboard snapshot while FINISHED stats are still replicated.
    void updateCachedPostMatchResult();

    /// @brief Enter chat input mode and release normal gameplay input capture.
    void openChat();

    /// @brief Leave chat input mode and restore normal gameplay input capture.
    void closeChat();

    /// @brief Send the current chat draft to the server and close the chat box.
    void submitChat();

    /// @brief Queue a replicated chat message for HUD display.
    void appendChatMessage(ClientId sender, std::string_view message);

    /// @brief Queue a locally-authored chat echo while waiting for server replication.
    void appendLocalChatMessage(std::string_view message);

    /// @brief Queue a generic transient HUD popup message.
    void appendPopupMessage(HudPopupKind kind, std::string_view message);

    /// @brief Clear held gameplay actions so typing chat cannot leak into movement or weapons.
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
    SfxSystem* sfxSystem = nullptr;                                ///< Borrowed audio system owned by App.
    UserSettings* userSettings = nullptr;                          ///< Borrowed user settings owned by App.
    std::string_view userSettingsPath_;                            ///< Borrowed save path for user settings.
    std::optional<registry_serialization::Loader> snapshotLoader_; ///< Incremental loader; created on first snapshot.
    std::optional<entt::entity>
        mappedLocalPlayerEntity_;  ///< Local-registry entity for this client's player, once assigned.
    ParticleSystem particleSystem; ///< Client-side VFX particle system.
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

    std::array<PredictedPlayerState, InputRingBuffer::k_capacity> predictedStateHistory_{};
    bool mouseCaptured = true; ///< True when relative mouse mode is active.

    /// @brief Currently-bound gamepad, or nullptr if none is plugged in.
    ///
    /// Opened by scanForConnectedGamepads() at init (for pads already plugged
    /// in before the match) and on SDL_EVENT_GAMEPAD_ADDED for runtime hot-plug
    /// (first device wins — extra controllers are ignored until the active one
    /// disconnects), closed on SDL_EVENT_GAMEPAD_REMOVED.  SDL3's gamepad
    /// mapping database normalises
    /// every supported controller (Xbox 360 / One, DualShock, Switch Pro, ...)
    /// onto the same logical buttons + axes, so the input mapping in
    /// InputSampleSystem.hpp works uniformly across devices.
    SDL_Gamepad* activeGamepad_ = nullptr;
    /// @brief SDL_JoystickID of the active gamepad — needed to identify the
    /// device on SDL_EVENT_GAMEPAD_REMOVED so we don't tear down a different
    /// controller when a second one disconnects.
    SDL_JoystickID activeGamepadId_ = 0;
    /// @brief Last input device the player actually used, for HUD glyph
    /// selection. Flips to Controller on gamepad button/stick/trigger input and
    /// back to KeyboardMouse on key/mouse input. Value-init is KeyboardMouse (0).
    BindingDevice lastInputDevice_{};
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

    /// @brief Local player's dead state last frame — used to detect the
    ///        dead→alive (respawn) edge and snap the view to the spawn yaw.
    bool localWasDead_ = false;

    // Runtime-tunable loop settings (exposed via ImGui)
    float mouseSensitivity = user_settings::kDefaultMouseSensitivity; ///< Radians per pixel of mouse movement.
    float horizontalFovDegrees = 90.0f;    ///< Player-facing horizontal camera field of view in degrees.
    bool renderSeparateFromPhysics = true; ///< Render every iterate() with interpolation (true)
                                           ///  vs only after a physics tick (false).
    bool inputSyncedWithPhysics = true;    ///< Sample mouse once per physics tick (true)
                                           ///  vs every iterate() call (false).
    bool limitFPSToMonitor = true;         ///< VSync on (true) / off (false). Default ON: caps to the
                                           ///  monitor refresh, which frees GPU headroom and removes the
                                           ///  near-light stutter. Toggle live via the debug menu; the
                                           ///  GROUP2_CLIENT_UNCAPPED env var / bench mode still force it off.

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

    // Emote state (client-side prediction for the local player). Set when the
    // emote wheel fires, cleared on local movement/combat input or death. Drives
    // the local animator override, the third-person emote camera, and showing
    // the local body. Remote players' emotes are server-driven via AnimSnapshot.
    int localEmote_{-1};        ///< Active local emote index (EmoteCatalog), or -1.
    float emoteCamBlend_{0.0f}; ///< 0 = first-person, 1 = third-person emote cam (eased).

    // Killcam: while dead and awaiting respawn, hold the camera at the death
    // position and rotate it to keep the killer centered on screen.
    bool killcamActive_{false};                    ///< True this frame while the killcam is driving the camera.
    glm::vec3 killcamEye_{0.0f};                   ///< Eye position locked at the moment of death.
    float killcamYaw_{0.0f};                       ///< Smoothed killcam yaw (radians).
    float killcamPitch_{0.0f};                     ///< Smoothed killcam pitch (radians).
    glm::vec3 killcamKillerCenter_{0.0f};          ///< Killer AABB center (world) for the HUD label.
    glm::vec3 killcamKillerHalf_{0.0f};            ///< Killer AABB half-extents (world) for the HUD label.
    entt::entity killcamKillerEntity_{entt::null}; ///< Killer entity (drives the chams pass), or null.
    std::string killcamKillerName_;                ///< Killer's display nickname (for the HUD label).

    // Map collision data — loaded from GLB, owns the vectors that back activeWorld().
    physics::MapCollisionData mapCollision_;

    // Central asset registry — maps human-readable names to renderer model indices.
    AssetRegistry assets_;

    // Legacy model index aliases (for code that still uses raw indices).
    // TODO: migrate all call sites to assets_.modelIndex("name") and remove these.
    int weaponModelIndices_[kRenderableWeaponTypeCount] = {-1, -1, -1, -1, -1};
    int weaponAssetIds_[kRenderableWeaponTypeCount] = {-1, -1, -1, -1, -1};
    int viewmodelLeftHandModelIdx_ = -1;
    int viewmodelRightHandModelIdx_ = -1;
    int handMountDebugMarkerModelIdx_ = -1;

    int rocketProjectileModelIdx_ = -1;
    int grenadeModelIdx_ = -1;
    int heGrenadeModelIdx_ = -1;
    int stickyGrenadeModelIdx_ = -1;
    int molotovModelIdx_ = -1;
    int medkitModelIdx_ = -1;
    int shieldPowerupModelIdx_ = -1;
    int damagePowerupModelIdx_ = -1;

    // Dynamic lighting test controls (ImGui-tunable)
    bool showDynLightUI_ = false;                        ///< Show the Dynamic Lighting panel.
    bool showHdrDebugUI_ = false;                        ///< Show the HDR / tonemap debug panel.
    bool showHudDebug_ = false;                          ///< Show the HUD Tweaker panel.
    bool showMenuThemeUI_ = false;                       ///< Show the Menu Theme Tweaker panel.
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
    struct BeamAudioState
    {
        bool active = false;
        float loopDelaySeconds = 0.0f;
        SfxSystem::SourceHandle loopHandle = SfxSystem::kInvalidSource;
    };
    std::unordered_map<entt::entity, BeamAudioState> beamAudioStates_;
    std::unordered_map<entt::entity, std::array<float, 5>> footstepPhases_;
    std::unordered_map<entt::entity, float> footstepCooldowns_;

    // Movement / ability transition tracking for SFX.
    struct PlayerSfxState
    {
        bool initialized = false;
        bool grounded = false;
        bool isDead = false;
        int moveMode = 0;
        bool gravityFlipped = false;
        bool grappleActive = false;
        float primaryCooldown = 0.0f;
        float secondaryCooldown = 0.0f;
        SfxSystem::SourceHandle slideLoopHandle = SfxSystem::kInvalidSource;
    };
    std::unordered_map<entt::entity, PlayerSfxState> playerSfxState_;

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

    // Shotgun pellet accumulator — groups the 9 NetParticleEvents emitted per
    // shotgun shot (by the server's WeaponSystem) into a single completed blast
    // for the HUD ShotgunPelletWidget. The widget is fed via
    // `HudGameState.latestShotgunBlast`, which is just a copy of
    // `lastShotgunBlast_` updated each frame with elapsed time.
    HudShotgunBlast shotgunPelletAccum_{}; ///< In-flight accumulator (resets when 9 pellets received).
    int shotgunPelletAccumCount_ = 0;      ///< Pellets received for the current in-flight blast.
    float shotgunPelletLastTimeSec_ = 0.f; ///< Game-time of the last pellet (for stale-reset).
    HudShotgunBlast lastShotgunBlast_{};   ///< Most recently completed blast (staged for HUD).

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

    /// @brief Pickup notifications queued for the next HUD frame.  Emitted
    /// when the local player's WeaponState gains a new weapon type.
    std::vector<HudPickupNotification> pendingPickupNotifications_;

    /// @brief Client-side countdown for the local damage powerup HUD frame effect.
    float damagePowerupHudTimer_ = 0.0f;
    float damagePowerupHudDuration_ = 1.0f;

    /// @brief Generic popup notifications queued for the next HUD frame.
    ///
    /// The HUD widget consumes each entry once, then owns display lifetime,
    /// animation, and expiration.
    std::vector<HudPopupMessage> pendingPopupMessages_;
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

    // Apex-style spring-damped camera recoil (actually deflects aim — bullets drift).
    // Each frame the spring integrates toward `target`, then the per-frame delta is
    // committed to `InputSnapshot.pitch`/`yaw` on the local player so raycasts,
    // tracers, bullet-holes, and the replicated aim all follow the recoil. Target
    // accumulates per shot during sustained fire and only decays back toward 0
    // after `recoilIdleTime_` exceeds the idle threshold — that's what produces
    // the auto-recovery feel without fighting active firing.
    bool useSpringCameraRecoil_ = true;         ///< Toggle Apex-spring path vs legacy instant snap-pitch write.
    float cameraRecoilPitch_ = 0.0f;            ///< Spring current offset (radians; negative = looking up).
    float cameraRecoilYaw_ = 0.0f;
    float cameraRecoilPitchVel_ = 0.0f;         ///< Spring velocity (rad/s).
    float cameraRecoilYawVel_ = 0.0f;
    float cameraRecoilTargetPitch_ = 0.0f;      ///< Spring target; accumulates per shot, decays only when idle.
    float cameraRecoilTargetYaw_ = 0.0f;
    float committedRecoilPitch_ = 0.0f;         ///< Portion of `cameraRecoilPitch_` already added to snap.pitch.
    float committedRecoilYaw_ = 0.0f;           ///< Each frame we commit `(current - committed)` and update.
    float recoilPatternScaleMultiplier_ = 2.0f; ///< Live multiplier on WeaponConfig.recoilPatternScale.
                                                ///< 1.0 = nominal; 2.0 = double recoil. Tune in debug UI.
    float cameraRecoilOmega_ = 35.0f;           ///< Spring angular frequency (rad/s). Higher = snappier kick.
    float cameraRecoilTargetDecay_ = 5.0f;      ///< Idle-recovery decay rate (1/s). Higher = faster snap-back.
    float cameraRecoilIdleThreshold_ = 0.18f;   ///< Seconds off-trigger before target starts decaying.

    // Recovery model — switch live between:
    //   A) No recovery (CS-style). Aim stays where the recoil walked it; the
    //      player owns 100% of the pull-down.
    //   B) Compensated recovery (Apex-like). Auto-recovers after fire-off, BUT
    //      while firing we measure the player's counter-mouse and subtract it
    //      from the recovery debt — so the engine refunds only the un-paid
    //      portion. Avoids the "double-compensation" crosshair-drop bug.
    bool useRecoilCompensation_ = false;    ///< false = Approach A (no restore — aim keeps the
                                            ///< view angles the recoil walked it to; player
                                            ///< compensates manually); true = Approach B.
    float lastSnapPitchAfterCommit_ = 0.0f; ///< snap.pitch saved at end of last spring tick — diff against
    float lastSnapYawAfterCommit_ = 0.0f;   ///< current to recover the player's mouse delta this frame.
    bool haveLastSnap_ = false;             ///< Becomes true after the first frame the local player exists.

    // Credit accumulator (Approach B). Each frame, the player's counter-mouse is
    // banked into a signed credit (capped + slowly-decaying). The credit is then
    // consumed against target — pre-payment of future kicks is preserved, and
    // continued pull after fire-off still cancels recovery debt. Without this
    // (cap-at-current-target naive comp), excess mouse input was wasted and the
    // recovery refund overcompensated whatever debt remained.
    float compensationCreditPitch_ = 0.0f;
    float compensationCreditYaw_ = 0.0f;
    float compensationCreditCap_ = 0.10f;  ///< Max banked credit (rad). ~5.7° per axis.
    float compensationCreditDecay_ = 1.0f; ///< Credit decay rate (1/s); stale credit fades.

    // Visual reload state
    float reloadDownwardOffset_ = 0.0f;       ///< Downward offset for the reload animation
    float grenadeThrowDownwardOffset_ = 0.0f; ///< Downward offset for the grenade-throw animation

    // Local weapon fire cooldown (mirrors server's per-weapon cooldown for VFX)
    float localFireCooldown_ = 0.0f; ///< Countdown timer; fire VFX only when <= 0.
    float localRecoilHeat_ = 0.0f;
    float recoilIdleTime_ = 0.0f;

    int pendingScrollSwitch_ = 0; ///< +1 = next slot, -1 = previous slot, consumed each frame.

    // Third-person weapon tuning (per weapon type, live-adjustable via ImGui)
    ThirdPersonWeaponParams tpWeaponParams_[kRenderableWeaponTypeCount]; ///< Runtime-tunable procedural/scale params.
    int tpTuneWeaponIdx_ = 0;         ///< Which weapon type the Weapon Hold tweaker is editing.
    bool tpFreezeAnimations_ = false; ///< Debug: freeze every animator's playback while tuning.

    // Third-person FK weapon hold poses (spine-relative gun + per-bone arm angles).
    std::array<WeaponHoldPose, kRenderableWeaponTypeCount> weaponHoldPoses_{};         ///< Runtime, live-tuned.
    std::array<WeaponHoldPose, kRenderableWeaponTypeCount> authoredWeaponHoldPoses_{}; ///< Compile-time defaults.
    std::array<std::string, kRenderableWeaponTypeCount> weaponHoldPosePaths_{};        ///< Source TOML path each.
    std::array<std::filesystem::file_time_type, kRenderableWeaponTypeCount> weaponHoldPoseMTimes_{};
    float holdPoseReloadAccumulator_ = 0.0f; ///< Seconds since last hot-reload mtime poll (~4 Hz throttle).
    float holdRotStepDeg_ = 5.0f;            ///< Delta-rotation step (deg) for the spine-rotation buttons.
    bool holdShowDebugMarker_ = true;        ///< Render the spine-anchor axis marker when the tweaker is open.
    bool showWeaponHoldUI_ = false;          ///< Show the Weapon Hold tweaker window.

    // First-person arm mount tuning (separate from third-person grips).
    FirstPersonHandMountParams
        fpHandMountParams_[kRenderableWeaponTypeCount];         ///< Runtime-tunable copy; initialised from defaults.
    FirstPersonHandMountParams
        authoredFPHandMountParams_[kRenderableWeaponTypeCount]; ///< Defaults loaded from authored weapon assets.
    int fpHandMountTuneWeaponIdx_ = 0;                          ///< Which weapon type is being tuned.
    bool showFPHandMountUI_ = false;                            ///< Show the FP Arm Tweaker window.

    enum class HandMountDebugSpace : std::uint8_t
    {
        None,
        ThirdPerson,
        FirstPerson,
    };

    enum class HandMountDebugPoint : std::uint8_t
    {
        Shoulder,
        Elbow,
        Palm,
        Finger0,
        Finger1,
        Finger2,
        Finger3,
        Finger4,
    };

    struct HandMountDebugTarget
    {
        HandMountDebugSpace space = HandMountDebugSpace::None;
        int weaponIdx = 0;
        bool left = false;
        HandMountDebugPoint point = HandMountDebugPoint::Palm;
    };

    HandMountDebugTarget handMountDebugTarget_{};

    // Weapon spawner model tuning (per weapon type, live-adjustable via ImGui)
    WeaponSpawnerModelParams
        spawnerWeaponParams_[kRenderableWeaponTypeCount]; ///< Runtime-tunable copy; initialised from spawner defaults.
    int spawnerTuneWeaponIdx_ = 0;                        ///< Which weapon type is being tuned.
    bool showWeaponSpawnerModelUI_ = false;               ///< Show the Weapon Spawner Model Tweaker window.

    // Animation subsystem — shared rig + clip library + skinning backend.
    // CharacterAnimators (one per animated entity) hold non-owning refs.
    CharacterRig charRig_;       ///< Shared skinned rig (skeleton + bind pose + weights).
    int rightHandJointIdx_ = -1; ///< Cached "mixamorig:RightHand" joint index (-1 = not present).
    int spine2JointIdx_ = -1;    ///< Cached "mixamorig:Spine2" joint index. The weapon is a rigid child of this bone
                                 ///< and the FK arm hold hangs off it.
    /// Per-entity weapon-swap fade state. When an entity's equipped weapon
    /// changes we reset `swapElapsedSec` to 0 and ramp the arm-FK hold weight
    /// 0 → 1 over the swap duration so the arms re-pose onto the new weapon.
    struct GripSwapState
    {
        WeaponType lastType = WeaponType::Rifle;
        float swapElapsedSec = 1.0f; ///< Initialised to "already done" so freshly-seen entities start at full hold.
    };
    std::unordered_map<entt::entity, GripSwapState> gripSwapState_{};
    float aimAssistParityAccumSec_ = 0.0f; ///< Seconds since the last aim-assist parity check log line (Phase F).
    AnimationLibrary animLibrary_;         ///< Collection of ozz clips on the shared rig.
    CpuLbsSkinningBackend skinBackend_;    ///< Phase-1 CPU linear-blend-skinning backend.
    AnimationTesterState animUI_;          ///< Persistent state for the Animation Tester panel.
    HitboxRig clientHitboxRig_;            ///< Hitbox definitions for client-side debug visualization.
    float kRigScale_ = 1.0f;               ///< Per-renderable scale for animated characters (auto-calculated, tunable).
    float kRigVerticalOffset_ =
        -90.0f;                ///< Per-renderable Y translation for animated characters (auto-calculated, tunable).
    float rigMeshMinY_ = 0.0f; ///< Minimum Y of the bind-pose mesh vertices (model space).

    /// Entities for which a death-dissolve particle burst has already been
    /// spawned (so we emit it exactly once per death). Cleared per-entity on
    /// respawn. See the death-capture pass in the render submit.
    std::unordered_set<entt::entity> dissolveSpawned_;

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
    int statsRenderFrames = 0;      ///< Rendered frames accumulated since last snapshot.
    float measuredPhysicsHz = 0.0f; ///< Computed physics rate (Hz).
    float statsFPSCurrent = 0.0f;   ///< Average render FPS over the last stats window (actual frames / elapsed time).
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

    /// Per-frame phase-time breakdown. Captured every frame in bench mode and
    /// by GROUP2_CLIENT_PERF=1 play-session recordings.
    std::vector<ClientPerfFrame> benchFrameStats_;
    ClientPerfRecorder perfRecorder_;
    float perfSnapshotApplyMs_ = 0.0f;
    std::uint32_t perfSnapshotApplyCount_ = 0;

    /// @brief Attach a fresh `AnimatedCharacter` component to an entity.
    ///
    /// Creates a new CharacterAnimator wired to the shared rig + clip library +
    /// skinning backend, uploads a per-entity clone of the rig's template model,
    /// and emplaces the component.  Safe to call even if the rig failed to load
    /// (logs a warning and leaves the entity un-animated).
    void attachAnimatedCharacter(entt::entity e);

    /// @brief Dispatcher subscriber: when ANY player fires a shot, push a
    /// per-weapon-class additive pitch impulse onto that player's spine. The
    /// kick decays inside `CharacterAnimator::runSamplingAndSkinning` so the
    /// upper body rises and settles back to the sampled aim pose over ~250 ms.
    /// Wired to the same `WeaponFiredEvent` channel SfxSystem listens on, so
    /// the same event drives both audio and visuals.
    void onWeaponFired(const struct WeaponFiredEvent& evt);

    /// @brief Open the gamepad with the given SDL_JoystickID and make it the
    /// active controller, but only if none is currently bound (first-device-wins
    /// policy).  Shared by the SDL_EVENT_GAMEPAD_ADDED hot-plug path and the
    /// init-time scan below, so both routes apply identical state + logging.
    void adoptGamepad(SDL_JoystickID id);

    /// @brief Open the first already-connected gamepad, if any.
    ///
    /// SDL fires SDL_EVENT_GAMEPAD_ADDED for pads present at SDL_Init time, but
    /// those events are delivered to whatever screen is active then (the lobby /
    /// Home), not the Game which doesn't exist yet — so a controller plugged in
    /// before the match starts would otherwise never be bound until the user
    /// physically reconnected it.  Game::init() calls this to enumerate
    /// SDL_GetGamepads() and adopt the first one, covering the "connected before
    /// launch" case; the event handler still covers runtime hot-plug.
    void scanForConnectedGamepads();

    // Match State
    MatchPhase currentMatchPhase = MatchPhase::LOBBY; ///< Latest match phase update from the server.
    ClientId currentWinnerId = ClientId{-1};          ///< ClientId of the current match winner, if in POSTMATCH.
    float countdownTimer = 0.0f; ///< Countdown timer for transitions between match phases (e.g. warmup to in-progress).
    std::optional<PostMatchResult> cachedPostMatchResult_; ///< Final scoreboard snapshot captured during FINISHED.
    bool returnToLobbyRequested = false;         ///< Latched true when server sends MATCH_STATE with phase == LOBBY.
    bool returnToMainMenuRequested_ = false;     ///< Latched true when the pause menu or disconnect requests leaving.
    bool serverShutdownNoticeRequested_ = false; ///< Latched true when leaving because the server connection closed.

    // Kill Feed State
    std::vector<KillFeedEvent> killFeed; ///< Recent kill events for on-screen kill feed (newest first).

    PauseMenu pauseMenu; ///< In-game pause menu (opened with ESC, blocks input to the game when active).

    // Bullet tracer muzzle
    glm::vec3 cachedMuzzleWorld_{0.0f};
    bool cachedMuzzleValid_ = false;

    // Local player's right-palm world position, cached from the viewmodel pass
    // each frame. Used as a muzzle-flash fallback when the weapon has no muzzle marker.
    glm::vec3 cachedRightPalmWorld_{0.0f};
    bool cachedRightPalmValid_ = false;

    /// @brief World position to spawn local weapon particles.
    ///
    /// Prefer the weapon model's tagged muzzle marker (`is_muzzle` / socket_muzzle)
    /// when available; fall back to the old palm-derived point for untagged guns.
    [[nodiscard]] glm::vec3 muzzleFlashOrigin(const glm::vec3& fallback) const;

    /// @brief Camera-relative railgun beam start used while scoped.
    [[nodiscard]] glm::vec3 scopedRailgunBeamOrigin() const;

    /// @brief Offset a muzzle point to the actual transient point-light position.
    ///
    /// `muzzleFlashLightOffset_` uses camera/viewmodel basis units:
    /// x = forward, y = up, z = right.
    [[nodiscard]] glm::vec3 muzzleFlashLightPosition(const glm::vec3& muzzleOrigin) const;

    glm::vec3 muzzleFlashLightOffset_{-12.25f, 0.0f, -10.0f}; ///< Tunable point-light offset from the muzzle (fwd/up/right).

    // Transient VFX point lights. Muzzle flashes and explosions both feed this
    // short-lived list, then iterate() fades the survivors into the renderer's
    // dynamic point-light array.
    struct TransientVfxLight
    {
        glm::vec3 position{0.0f}; ///< World position at spawn time.
        glm::vec3 color{1.0f};    ///< Flash colour (linear RGB).
        float intensity = 0.0f;   ///< Peak intensity at spawn (faded by age).
        float age = 0.0f;         ///< Seconds elapsed since spawn.
        float lifetime = 0.06f;   ///< Total flash duration in seconds.
        float range = 500.0f;     ///< Renderer light attenuation range.
    };
    std::vector<TransientVfxLight> transientVfxLights_;

    /// @brief Spawn a transient muzzle-flash point light at @p pos.
    void spawnMuzzleFlashLight(const glm::vec3& pos, WeaponType weaponType);

    /// @brief Spawn a transient explosion point light at @p pos.
    void spawnExplosionFlashLight(const glm::vec3& pos, WeaponType weaponType, float radius);
};
