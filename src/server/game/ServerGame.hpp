/// @file ServerGame.hpp
/// @brief Top-level server game loop integrating ECS and networking.

#pragma once

#include "client/animation/AnimationLibrary.hpp"
#include "client/animation/CharacterAnimator.hpp"
#include "client/animation/CharacterRig.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/physics/MapLoader.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/NetworkConfig.hpp"
#include "network/Server.hpp"
#include "systems/MatchController.hpp"

#include <SDL3/SDL.h>

#include <entt/entity/entity.hpp>
#include <memory>
#include <unordered_map>

/// @brief Top-level server game loop.
///
/// Owns the ECS registry and the network Server. Each tick it drains
/// incoming messages, runs all ECS systems, and broadcasts state.
class ServerGame
{
public:
    /// @brief Bind to the given address and port, spawn test entities.
    /// @param addr        Hostname or IP to bind to (e.g. "127.0.0.1").
    /// @param port        TCP port to listen on.
    /// @param tickRateHz  Physics tick rate in Hz (default 128).
    /// @param snapshotHz  Registry snapshot send rate in Hz (default 32).
    ///                    Must be ≤ tickRateHz; clamped if not. Phase 4
    ///                    decouples snapshot rate from tick rate so the
    ///                    server can keep deterministic 128 Hz physics
    ///                    while only paying the serialization+broadcast
    ///                    cost a fraction as often.
    /// @param transport   Phase 3d: UDP sidecar feature toggles.
    /// @return True on success, false on network or initialisation failure.
    bool init(const char* addr,
              Uint16 port,
              int tickRateHz = 128,
              int snapshotHz = 32,
              const TransportConfig& transport = {});

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

private:
    /// @brief Apply a single event to the ECS registry.
    /// @param event The event to process.
    void eventHandler(Event event);

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

    physics::MapCollisionData mapCollision_; ///< Map collision data — owns vectors backing activeWorld().

    Server server;                           ///< Owns the TCP socket and network I/O.
    Registry registry;                       ///< ECS entity/component store.
    MatchController matchController;         ///< Manages match flow and state.
    std::unordered_map<ClientId, entt::entity> clientEntities; ///< Maps client IDs to ECS entities.
    std::vector<NetKillEvent> pendingKillEvents; ///< Accumulates kill events waiting for network broadcast.
    bool running = false;                        ///< Loop continues while true.
    int tickRateHz = 128;                        ///< Physics ticks per second.
    int tickCount = 0;                           ///< Total ticks since start, used for periodic logging.

    /// @brief Send a registry snapshot every Nth tick. Computed in init() as
    /// `max(1, tickRateHz / snapshotHz)` so the snapshot rate is roughly
    /// `tickRateHz / snapshotEveryNTicks` Hz. With the default 128 / 32 = 4
    /// the server snapshots every 4th tick — 4× less serialization +
    /// broadcast work than pre-Phase-4.
    int snapshotEveryNTicks = 4;

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
};
