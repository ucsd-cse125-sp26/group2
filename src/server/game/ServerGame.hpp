/// @file ServerGame.hpp
/// @brief Top-level server game loop integrating ECS and networking.

#pragma once

#include "client/animation/AnimationLibrary.hpp"
#include "client/animation/CharacterAnimator.hpp"
#include "client/animation/CharacterRig.hpp"
#include "ecs/abilities/AbilityRegistry.hpp"
#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/PlayerColors.hpp"
#include "ecs/components/PlayerNicknames.hpp"
#include "ecs/physics/ContactCache.hpp"
#include "ecs/physics/MapLoader.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/NetworkConfig.hpp"
#include "network/Server.hpp"
#include "server/lobby/LobbyManager.hpp"
#include "systems/Event.hpp" // PR-27: ShotIntentPayload
#include "systems/MatchController.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <entt/entity/entity.hpp>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

/// @brief Top-level server game loop.
///
/// Borrows an already-bound Server from the caller (main owns the socket).
/// Each tick it drains incoming messages, runs all ECS systems, and broadcasts state.
class ServerGame
{
public:
    /// @brief Attach to an already-bound Server and initialise game state.
    /// @param server      Externally-owned, already-initialised Server.
    /// @param tickRateHz  Physics tick rate in Hz (default 128).
    /// @param snapshotHz  Registry snapshot send rate in Hz (default 32).
    ///                    Must be ≤ tickRateHz; clamped if not.
    /// @param skipLobby   True to bypass lobby and enter countdown automatically.
    /// @return True on success, false on initialisation failure.
    bool init(Server& server, int tickRateHz = 128, int snapshotHz = 32, bool skipLobby = false);

    /// @brief Block on the game loop until shutdown() is called.
    ///
    /// Loop structure (each iteration = one tick at tickRateHz):
    ///  1. `server.poll()` — accept new connections, read incoming packets,
    ///     enqueue events (Connected / Disconnected / Input).
    ///  2. `tick(dt, nextTick)` — process events and run all ECS systems.
    ///  3. **Sleep** — hybrid sleep+spin-wait to maintain tick cadence.
    ///
    /// @see tick for the per-tick ECS system execution order.
    void run();

    /// @brief Signal the loop to stop and release all resources.
    void shutdown();

    /// @brief True if the given client currently owns host-only lobby/server controls.
    [[nodiscard]] bool isHost(ClientId clientId) const;

    /// @brief Register a callback fired after the host updates match settings.
    void onMatchConfigUpdated(std::function<void(const MatchConfig&)> fn) { matchConfigUpdatedFn_ = std::move(fn); }

    /// @brief Register a callback fired after the host updates discovery visibility.
    void onDiscoverySettingsUpdated(std::function<void(const DiscoverySettings&)> fn)
    {
        discoverySettingsUpdatedFn_ = std::move(fn);
    }

    /// @brief Apply a complete match config to the authoritative match controller.
    bool setMatchConfig(const MatchConfig& config);

    /// @brief Update only the kill threshold in the authoritative match config.
    bool setKillsToWin(int kills);

    /// @brief Update only the maximum accepted player count in the authoritative match config.
    bool setMaxPlayers(int maxPlayers);

    /// @brief Configure idle shutdown timeout in minutes; non-positive values disable it.
    bool setIdleShutdownMinutes(int minutes);

private:
    /// @brief Apply a single event to the ECS registry.
    /// @param event The event to process.
    void eventHandler(const Event& event);
    void applyInputEvent(ClientId clientId, const InputSnapshot& inputSnapshot);

    /// @brief Advance one physics tick: drain events, run ECS systems, broadcast state.
    ///
    /// Execution order each tick:
    ///  1. **Event drain** — dequeue Connected/Disconnected/Input events from the
    ///     network server until the queue is empty or the tick deadline is exceeded.
    ///  2. **Animation + hitboxes** — `updateAnimationAndHitboxes(dt)` samples
    ///     skeleton poses and recomputes bone-capsule hitboxes for all players.
    ///  3. **Weapon system** — `runWeapon()` processes fire inputs, performs
    ///     hitscan raycasts against hitbox capsules, applies damage, generates
    ///     particle/kill events.
    ///  4. **Movement** — `runMovement()` applies acceleration, friction, gravity,
    ///     and special movement modes (wallrun, slide, grapple).
    ///  5. **Collision** — `runCollision()` performs swept-AABB resolution against
    ///     the world geometry (planes, boxes, brushes).
    ///  6. **Explosions** — `runExplosion()` processes pending projectile detonations
    ///     with radius damage.
    ///  7. **Player status** — `runPlayerStatus()` handles respawn timers, death
    ///     state transitions, and health regeneration.
    ///  8. **Weapon spawners** — `runWeaponSpawners()` ticks pickup cooldowns and
    ///     spawns weapon entities.
    ///  8b. **Dropped weapons** — `runDroppedWeapons()` handles pickup and
    ///     despawn for player-dropped weapons.
    ///  9. **Match controller** — `matchController.update()` manages match phase
    ///     transitions (warmup → countdown → in-progress → finished).
    /// 10. **Broadcast** — send updated registry snapshot, particle events, and
    ///     kill events to all connected clients.
    ///
    /// @param dt       Fixed delta time in seconds (1 / tickRateHz).
    /// @param nextTick Performance counter deadline for the current tick.
    /// @see Game::iterate for the client-side frame loop.
    void tick(float dt, Uint64 nextTick);

    /// @brief Create a new player entity and map it to the given client ID.
    /// @param clientId Network client identifier for the new player.
    void initNewPlayerEntity(ClientId clientId);

    /// @brief Remove player entity from ECS.
    /// @param clientId Network client identifier for the player.
    void deletePlayerEntity(ClientId clientId);

    /// @brief Initialise the server-side animation subsystem (skeleton, clips, hitboxes).
    /// Called once during init() after map loading.
    void initAnimation();

    /// @brief Create and store a server-side animator for the given player entity.
    void attachServerAnimator(entt::entity player);

    /// @brief Remove the server-side animator for the given entity.
    void detachServerAnimator(entt::entity player);

    /// @brief Update all server-side animators and recompute hitbox capsules.
    /// Called once per tick before weapon/damage systems.
    void updateAnimationAndHitboxes(float dt);

    /// @brief Phase 6: write `LagCompTarget` onto each connected
    /// player's entity from their connection's last-reported RTT.
    ///
    /// Translates `Connection::lastReportedRttMs` (ms) into a
    /// `targetServerTick = max(0, currentServerTick - rewindTicks)`,
    /// where `rewindTicks = clamp(rttMs * tickRateHz / 2000, 0,
    /// k_maxLagCompTicks)`. Players with no client connection (e.g.
    /// AI bots in a future expansion) keep their previous target,
    /// which on the next pushHitboxHistory will become a valid
    /// rewind anchor — but for now, only entities bound through
    /// `clientEntities` get a target.
    ///
    /// Called once per tick between `pushHitboxHistory` and
    /// `runWeapon`.
    void updateLagCompTargets();

    /// @brief Select the subset of abilities for new match.
    void selectMatchAbilityPool();
    void applyMatchAbilityChoices(AbilityState& state) const;

    physics::MapCollisionData mapCollision_;        ///< Map collision data — owns vectors backing activeWorld().

    Server* server = nullptr;                       ///< Non-owning pointer; main() owns and shuts down the socket.
    Registry registry;                              ///< ECS entity/component store.
    AbilityRegistry abilityRegistry;                ///< Registry of abilities via type idx.
    std::vector<AbilityType> matchPrimaryAbilities; ///< list of abilities available during the match
    std::vector<AbilityType> matchSecondaryAbilities;
    LobbyManager lobbyManager;                      ///< Owns lobby roster and validates host-initiated match starts.
    MatchController matchController;                ///< Manages match flow and state.
    std::function<void(const MatchConfig&)> matchConfigUpdatedFn_; ///< Mirrors match updates to networking/discovery.
    std::function<void(const DiscoverySettings&)>
        discoverySettingsUpdatedFn_;                               ///< Mirrors discovery visibility changes.
    bool lobbyStartCountdownActive = false; ///< True while lobby is counting down before entering match countdown.
    float lobbyStartCountdownTimer = 0.0f;  ///< Seconds remaining in the lobby staging countdown.
    ClientId lobbyStartRequester{-1};       ///< Host that requested the active lobby staging countdown.
    std::unordered_map<ClientId, entt::entity> clientEntities; ///< Maps client IDs to ECS entities.
    std::vector<NetKillEvent> pendingKillEvents; ///< Accumulates kill events waiting for network broadcast.

    /// @brief Use-count per palette slot for least-used color reservation.
    ///
    /// Sized to player_colors::k_paletteSize. Incremented when a player
    /// connects (assigns the smallest-count slot, ties broken by lowest
    /// index for determinism); decremented on disconnect.
    std::array<int, player_colors::k_paletteSize> colorSlotUseCounts_{};

    /// @brief Use-count per nickname slot — same selection scheme as colors.
    /// Decrements when an *auto-assigned* nickname is released; custom
    /// nicknames don't touch the count, since they don't reserve a slot.
    std::array<int, player_nicknames::k_nicknameCount> nicknameSlotUseCounts_{};
    bool running = false; ///< Loop continues while true.
    int tickRateHz = 128; ///< Physics ticks per second.
    int tickCount = 0;    ///< Total ticks since start, used for periodic logging.

    /// @brief Send a registry snapshot every Nth tick. Computed in init() as
    /// `max(1, tickRateHz / snapshotHz)` so the snapshot rate is roughly
    /// `tickRateHz / snapshotEveryNTicks` Hz. With the default 128 / 32 = 4
    /// the server snapshots every 4th tick — 4× less serialization +
    /// broadcast work than pre-Phase-4.
    int snapshotEveryNTicks = 4;
    bool shotDebugEnabled_ = false; ///< Opt-in lag-comp shot visualizer capture/send path.

    // ── Phase 6+ rigid-body dynamics state ──
    /// @brief Persistent contact-manifold cache for warm-starting the PGS
    /// solver across ticks.  Cleared on map load; trimmed each tick by
    /// `runDynamics`.
    physics::ContactCache contactCache_;

    // ── Server-side animation subsystem ──
    CharacterRig serverRig_;             ///< Shared skeleton (loaded from same FBX as client).
    AnimationLibrary serverAnimLibrary_; ///< Animation clips for server-side sampling.
    HitboxRig hitboxRig_;                ///< Shared hitbox capsule definitions.
    float rigScale_ = 1.0f;              ///< Rig model-space → game-unit scale factor.
    float rigMeshMinY_ = 0.0f;           ///< Minimum Y of bind-pose mesh (for vertical offset).
    bool animationLoaded_ = false;       ///< True if rig+clips loaded successfully.

    /// Per-entity server animators (not ECS components to avoid pulling animation
    /// headers into the component registry).
    std::unordered_map<entt::entity, std::unique_ptr<CharacterAnimator>> serverAnimators_;

    // ── PR-18: server-side ground-truth log ──────────────────────────────
    //
    // Opened from `GROUP2_SERVER_TRUTH_CSV` env var if set.  One global
    // CSV file containing one row per replicated player per logged tick:
    //
    //     wallTimeNs,serverTick,clientId,posX,posY,posZ
    //
    // Throttled to every Nth tick (default 4 = 32 Hz at the 128 Hz tick
    // rate; tunable via `GROUP2_SERVER_TRUTH_HZ_DIVIDER`).  Log rate is
    // chosen to be high enough that linear interpolation between
    // adjacent samples is a good approximation of "the server's
    // position at any wall-clock time T", which is what the offline
    // analyzer compares against bot-side observations.  At 32 Hz × 100
    // bots × ~50 B/row = ~160 KB/s — trivial disk I/O cost on any
    // reasonable test rig.
    //
    // No mutex needed: this runs on the game thread, on the same path
    // as the existing tick logic, after the per-tick physics + lag-comp
    // updates have settled.
    std::FILE* truthCsv_ = nullptr;
    int truthHzDivider_ = 4;

    /// @brief Open the ground-truth CSV from env var if set.  No-op when
    /// the env var is missing; load tests stay fast by default.
    void openGroundTruthLog();

    /// @brief Write one row per replicated player entity if the current
    /// `tickCount` aligns with `truthHzDivider_`.  Called at the end of
    /// each tick after the per-tick physics + broadcast settles.
    void writeGroundTruthLogIfDue();

    /// @brief Flush + close the CSV if open.  Safe to call from dtor.
    void closeGroundTruthLog() noexcept;

    /// @brief Reset live players to spawn points for countdown
    void resetPlayersForCountdown();

    /// @brief Determine if server should allow input based on phase
    [[nodiscard]] bool isGameplayInputAllowed(MatchPhase phase) const;

    // ── PR-27: pending client SHOT_INTENTs ───────────────────────────────
    //
    // The network thread enqueues `EventType::ShotIntent` events into
    // `eventQueue`; the game thread's `processEvent` drains them and
    // stashes the payload here, keyed by `(shooterClientId,
    // shotInputTick)`.  When the weapon-system path resolves a shot
    // for the same shooter at the same input tick, it looks up the
    // intent here, computes the anim-state delta vs the historical
    // sample, and emits the result to `server_shots.csv`.
    //
    // Bounded at `k_pendingShotIntentsMax = 256` entries (~2 s of
    // shots at the 128 Hz fire-rate ceiling).  The map is single-
    // threaded read/write on the game thread.
    struct ShotIntentKey
    {
        std::uint16_t shooterClientId = 0;
        std::uint32_t shotInputTick = 0;
        bool operator==(const ShotIntentKey& o) const noexcept
        {
            return shooterClientId == o.shooterClientId && shotInputTick == o.shotInputTick;
        }
    };
    struct ShotIntentKeyHash
    {
        std::size_t operator()(const ShotIntentKey& k) const noexcept
        {
            // Mix shooter into the high bits so two shots from the
            // same shooter at adjacent ticks aren't bucket-adjacent.
            return (static_cast<std::size_t>(k.shooterClientId) << 32) ^ k.shotInputTick;
        }
    };
    static constexpr std::size_t k_pendingShotIntentsMax = 256;
    std::unordered_map<ShotIntentKey, ShotIntentPayload, ShotIntentKeyHash> pendingShotIntents_;

    bool idleShutdownEnabled_ = false; ///< True if idle shutdown is enabled via env var.
    uint64_t idleShutdownMs_ = 0;      ///< Idle shutdown timeout in ms, from env var. 0 = no timeout.
    uint64_t lastNonEmptyMs_ = 0;      ///< Timestamp of the last player activity, for idle shutdown.
};
