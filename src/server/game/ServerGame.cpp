/// @file ServerGame.cpp
/// @brief Implementation of the server-side game loop, tick logic, and player management.

#include "ServerGame.hpp"

#include "client/animation/CharacterAnimator.hpp"
#include "ecs/AssetCatalog.hpp"
#include "ecs/MapConfig.hpp"
#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LagCompTarget.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerSimState.hpp" // also pulls in PlayerVisState
#include "ecs/components/Position.hpp"
#include "ecs/components/Renderable.hpp"
#include "ecs/components/RespawnPoint.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/ExplosionSystem.hpp"
#include "ecs/systems/HitboxSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "ecs/systems/WeaponSpawnerSystem.hpp"
#include "ecs/systems/WeaponSystem.hpp"
#include "network/PacketType.hpp"
#include "network/ShotDebugReport.hpp"
#include "network/ShotEvent.hpp"
#include "perf/Parallel.hpp"
#include "perf/Profiler.hpp"
#include "perf/ShotLog.hpp"
#include "server/systems/HitboxHistorySystem.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

bool ServerGame::init(const char* addr, Uint16 port, int hz, int snapshotHz, const TransportConfig& transport)
{
    tickRateHz = hz;

    // Phase 4a: snapshot rate ≤ tick rate. Both should be positive ints; we
    // re-clamp here in case the caller didn't (NetworkConfig already
    // clamps the loaded value to [1, 256]).
    const int clampedSnapshotHz = std::max(1, std::min(snapshotHz, hz));
    snapshotEveryNTicks = std::max(1, hz / clampedSnapshotHz);
    SDL_Log("[server] tickRate=%d Hz, snapshotRate≈%d Hz (every %d ticks)",
            hz,
            hz / snapshotEveryNTicks,
            snapshotEveryNTicks);

    clientEntities.clear(); // For safety
    registry.clear();

    // ── Load map collision ──────────────────────────────────────────────
    // Map filename and load-mode toggles live in ecs/MapConfig.hpp so the
    // client and server load the exact same primitives (a prerequisite for
    // prediction parity).  To switch maps, edit `kMapAsset` in AssetCatalog.hpp.
    // To change *how* the map is loaded, edit the constants in MapConfig.hpp.
    {
        gamemap::loadConfiguredMap(mapCollision_, "server");

        // Load prop collision — must match client for prediction parity.
        // Props with `decomposeCollision = true` (pallet, bottle) are non-convex,
        // so V-HACD turns each sub-mesh into a few `WorldBrush`es instead of a
        // `WorldTriMesh`.  Server pays a one-shot startup cost but runtime
        // collision is much smoother (no per-triangle jitter).
        //
        // PR-30: gated on `gamemap::k_useVhacd` so the team can flip the
        // behaviour project-wide from `ecs/MapConfig.hpp` without editing
        // per-call-site flags.  Must AND with the per-asset
        // `decomposeCollision` flag — both must agree before V-HACD runs.
        const char* const base = SDL_GetBasePath();
        const std::string assetsDir = std::string(base ? base : "") + "assets/";
        for (const AssetDefinition& def : kPropAssets) {
            const bool decompose = def.decomposeCollision && gamemap::k_useVhacd;
            physics::loadPropCollision(
                assetsDir + def.filename, mapCollision_, def.loadTranslation, def.loadScale, decompose);
        }

        // Set active world with map + all props.
        physics::setActiveWorld(mapCollision_.geometry());
    }

    if (!server.init(addr, port, transport))
        return false;

    // ── Load animation subsystem for hitbox detection ──
    initAnimation();

    // PR-18: open the server-side ground-truth log if requested.
    openGroundTruthLog();
    // PR-18b: open the server-side shot-resolution log if requested.
    // Implemented as a free function in `perf::shotlog` rather than a
    // ServerGame member because WeaponSystem.cpp (which calls it) is
    // shared between server and client TUs and shouldn't depend on
    // server-only types.
    ::group2::perf::shotlog::openIfRequested();

    return true;
}

void ServerGame::run()
{
    running = true;

    const float k_dt = 1.0f / static_cast<float>(tickRateHz);
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_tickDuration = k_perfFreq / static_cast<Uint64>(tickRateHz);
    Uint64 nextTick = SDL_GetPerformanceCounter();

    // temp weapon spawner
    const entt::entity energySpawner = registry.create();
    registry.emplace<WeaponSpawner>(
        energySpawner, WeaponSpawner{.type = WeaponType::EnergyGun, .spawnCooldown = 0.0, .hasWeapon = false});
    registry.emplace<Position>(energySpawner, glm::vec3{-100.0f, 15.0f, 0.0f});
    registry.emplace<CollisionShape>(energySpawner);

    // temp rocket spawner
    const entt::entity rocketSpawner = registry.create();
    registry.emplace<WeaponSpawner>(
        rocketSpawner, WeaponSpawner{.type = WeaponType::Rocket, .spawnCooldown = 0.0, .hasWeapon = false});
    registry.emplace<Position>(rocketSpawner, glm::vec3{-100.0f, 15.0f, 120.0f});
    registry.emplace<CollisionShape>(rocketSpawner);

    // temp rocket spawner
    const entt::entity rifleSpawner = registry.create();
    registry.emplace<WeaponSpawner>(rifleSpawner,
                                    WeaponSpawner{.type = WeaponType::Rifle, .spawnCooldown = 0.0, .hasWeapon = false});
    registry.emplace<Position>(rifleSpawner, glm::vec3{-100.0f, 15.0f, -120.0f});
    registry.emplace<CollisionShape>(rifleSpawner);

    // temp rail gun spawner
    const entt::entity railSpawner = registry.create();
    registry.emplace<WeaponSpawner>(
        railSpawner, WeaponSpawner{.type = WeaponType::RailGun, .spawnCooldown = 0.0, .hasWeapon = false});
    registry.emplace<Position>(railSpawner, glm::vec3{-100.0f, 15.0f, -240.0f});
    registry.emplace<CollisionShape>(railSpawner);

    // Static respawn points (with cooldown state)
    const entt::entity playerSpawner1 = registry.create();
    registry.emplace<RespawnPoint>(playerSpawner1, RespawnPoint{});
    registry.emplace<Position>(playerSpawner1, glm::vec3{-444.0f, 0.0f, 2000.0f});

    const entt::entity playerSpawner2 = registry.create();
    registry.emplace<RespawnPoint>(playerSpawner2, RespawnPoint{});
    registry.emplace<Position>(playerSpawner2, glm::vec3{800.0f, 40.0f, 800.0f});

    const entt::entity playerSpawner3 = registry.create();
    registry.emplace<RespawnPoint>(playerSpawner3, RespawnPoint{});
    registry.emplace<Position>(playerSpawner3, glm::vec3{800.0f, 430.0f, -800.0f});

    const entt::entity playerSpawner4 = registry.create();
    registry.emplace<RespawnPoint>(playerSpawner4, RespawnPoint{});
    registry.emplace<Position>(playerSpawner4, glm::vec3{0.0f, 200.0f, 0.0f});

    while (running) {
        server.poll();

        nextTick += k_tickDuration;
        tick(k_dt, nextTick);

        const Uint64 k_now = SDL_GetPerformanceCounter();
        if (k_now < nextTick) {
            const Sint64 k_sleepMs = static_cast<Sint64>((nextTick - k_now) * 1000 / k_perfFreq) - 1;
            if (k_sleepMs > 0)
                SDL_Delay(static_cast<Uint32>(k_sleepMs));

            // Spin-wait for the remaining sub-millisecond.
            while (SDL_GetPerformanceCounter() < nextTick) {
            }
        }
    }
}

void ServerGame::shutdown()
{
    running = false;
    server.shutdown();
    closeGroundTruthLog();
    ::group2::perf::shotlog::close();
}

// ── PR-18: server-side ground-truth log ─────────────────────────────────

void ServerGame::openGroundTruthLog()
{
    const char* path = std::getenv("GROUP2_SERVER_TRUTH_CSV");
    if (path == nullptr || path[0] == '\0')
        return;

    truthCsv_ = std::fopen(path, "w");
    if (truthCsv_ == nullptr) {
        SDL_Log("[server] PR-18: failed to open ground-truth log at %s", path);
        return;
    }
    std::fprintf(truthCsv_, "wallTimeNs,serverTick,clientId,posX,posY,posZ\n");
    std::fflush(truthCsv_);

    if (const char* div = std::getenv("GROUP2_SERVER_TRUTH_HZ_DIVIDER")) {
        const int parsed = std::atoi(div);
        if (parsed > 0)
            truthHzDivider_ = parsed;
    }
    SDL_Log("[server] PR-18: writing ground-truth log to %s (every %d ticks)", path, truthHzDivider_);
}

void ServerGame::writeGroundTruthLogIfDue()
{
    if (truthCsv_ == nullptr)
        return;
    if ((tickCount % truthHzDivider_) != 0)
        return;

    // Wall-clock at sample time.  The offline analyzer assumes server
    // and bot clocks are comparable — true on a single host (localhost
    // load test).  If we ever do split-host testing the analyzer will
    // need an explicit clock-skew estimator.
    const Uint64 nowNs = SDL_GetTicksNS();

    auto view = registry.view<const Position, const ClientId>();
    for (const auto e : view) {
        const auto& pos = view.get<const Position>(e);
        const auto& cid = view.get<const ClientId>(e);
        std::fprintf(truthCsv_,
                     "%llu,%d,%u,%.4f,%.4f,%.4f\n",
                     static_cast<unsigned long long>(nowNs),
                     tickCount,
                     static_cast<unsigned>(cid.value),
                     static_cast<double>(pos.value.x),
                     static_cast<double>(pos.value.y),
                     static_cast<double>(pos.value.z));
    }
    // Per-second flushes would suffice but per-write keeps the file
    // recoverable on a server crash.  At the throttled rate this is
    // ~32 fwrite syscalls/sec — negligible.
    std::fflush(truthCsv_);
}

void ServerGame::closeGroundTruthLog() noexcept
{
    if (truthCsv_ != nullptr) {
        std::fclose(truthCsv_);
        truthCsv_ = nullptr;
    }
}

void ServerGame::eventHandler(Event event)
{
    switch (event.type) {
    case EventType::Connected: {
        initNewPlayerEntity(event.clientId);
        const bool sent = server.notifyPlayerClientId(event.clientId, clientEntities[event.clientId]);
        if (!sent) {
            deletePlayerEntity(event.clientId);
        }
        break;
    }
    case EventType::Disconnected: {
        deletePlayerEntity(event.clientId);
        break;
    }
    case EventType::Input: {
        // Handle input snapshot
        const auto entityIt = clientEntities.find(event.clientId);
        if (entityIt == clientEntities.end())
            return;

        const entt::entity player = entityIt->second;
        if (!registry.valid(player))
            return;

        InputSnapshot& input = registry.get_or_emplace<InputSnapshot>(player);
        input = event.movementIntent;
        break;
    }
    case EventType::ShotIntent: {
        // PR-27: stash the per-shot client assertion under
        // `(shooterClientId, shotInputTick)` so the weapon-system path
        // can pick it up when it processes that tick's INPUT.  The
        // map is bounded — old keys age out (256 entries cap, ~2 s of
        // shots at the 128 Hz fire-rate ceiling).
        ShotIntentKey key{.shooterClientId = static_cast<std::uint16_t>(event.clientId.value),
                          .shotInputTick = event.shotIntent.shotInputTick};
        pendingShotIntents_[key] = event.shotIntent;
        if (pendingShotIntents_.size() > k_pendingShotIntentsMax) {
            // Evict the oldest entry — std::unordered_map iteration
            // order isn't stable but we just need SOMETHING to drop.
            // The intent isn't critical (server falls back to its own
            // anim history); this keeps memory bounded under abnormal
            // packet rates without requiring a separate LRU.
            pendingShotIntents_.erase(pendingShotIntents_.begin());
        }
        break;
    }
    default:
        break;
    }
}

void ServerGame::tick(float dt, Uint64 nextTick)
{
    // PR-1: per-tick wall clock. Recorded into the Profiler at end-of-tick
    // so `[perf …]` log lines and CSV rows always reflect the tick budget
    // including any work added by future PRs.
    const Uint64 tickStartCounter = SDL_GetPerformanceCounter();

    {
        GROUP2_PROF_SCOPE("eventDrain");
        // PR-2b: bulk-drain the event queue in a single mutex acquisition
        // instead of locking once per event. With ~5-input redundancy
        // dedup'd at the receive boundary, a 128 Hz × 500 bot fleet
        // pushes ~12 k unique inputs/sec. Pre-PR-2b that was 24 k mutex
        // operations/sec on stateMutex_, which contended hard with the
        // network thread's 1 kHz I/O cycle. Now: one lock per tick.
        static thread_local std::vector<Event> events;
        server.drainEvents(events);
        for (const Event& event : events) {
            eventHandler(event);
            // Tick-time bail-out kept identical to pre-PR-2b: if event
            // processing alone blows the tick budget we abort the rest
            // of the events (lost — TODO upstream the drop reason).
            if (const Uint64 kNow = SDL_GetPerformanceCounter(); kNow >= nextTick) {
                SDL_Log("[server] Exceeded tick time for event handling.");
                break;
            }
        }
    }

    {
        GROUP2_PROF_SCOPE("animation");
        // Update server-side animation and hitbox capsules before weapon raycasts.
        updateAnimationAndHitboxes(dt);
    }

    {
        GROUP2_PROF_SCOPE("hitboxHistoryPush");
        // Phase 6: capture this tick's capsules into each entity's HitboxHistory
        // ring. Has to run *after* updateHitboxes (so the capsules reflect this
        // tick's pose) and *before* runWeapon (so the upcoming hitscans share a
        // consistent history snapshot from which rewindHitboxes can pick).
        systems::pushHitboxHistory(registry, static_cast<uint32_t>(tickCount));
    }

    {
        GROUP2_PROF_SCOPE("lagcompTargets");
        // Phase 6: per-shooter lag-comp scheduler. For every player entity
        // bound to a connected client, set `LagCompTarget.targetServerTick`
        // to `currentServerTick - clamp(rttMs/2 → ticks, 0, k_maxLagCompTicks)`.
        // The hitscan path inside runWeapon reads this off the shooter
        // entity to size the rewind window for that shooter's shot.
        //
        // Capping at k_maxLagCompTicks (default ~25 ticks ≈ 200 ms at
        // 128 Hz) prevents pathologically high-ping shooters from
        // rewinding deep into the past, which would let them hit targets
        // that have long since moved out of cover and produce
        // "shot-around-the-corner" feel for the victims.
        updateLagCompTargets();
    }

    std::vector<NetParticleEvent> particleEvents;
    // PR-20: per-shot rewound state for the lag-comp debug visualiser.
    // Populated only when the weapon system fires a hitscan; sent
    // unicast to the shooter after the broadcast events block below.
    std::vector<net::shotdebug::ShotDebugCapture> shotDebugReports;
    {
        // PR-27: stash pending SHOT_INTENTs onto each shooter as a
        // transient `PendingShotIntent` component, keyed by the
        // shooter's CURRENT input tick.  `WeaponSystem::handleFire`
        // reads this when logging the shot to record the client-
        // asserted target id + anim state delta in `server_shots.csv`.
        // Anything left over after this loop runs is ageing out — the
        // map is also bounded by `k_pendingShotIntentsMax` (256
        // entries) on the enqueue side.
        for (const auto& [clientId, entity] : clientEntities) {
            if (!registry.valid(entity))
                continue;
            const auto* input = registry.try_get<InputSnapshot>(entity);
            if (input == nullptr) {
                registry.remove<PendingShotIntent>(entity);
                continue;
            }
            ShotIntentKey key{.shooterClientId = static_cast<std::uint16_t>(clientId.value),
                              .shotInputTick = input->tick};
            auto it = pendingShotIntents_.find(key);
            if (it != pendingShotIntents_.end()) {
                registry.emplace_or_replace<PendingShotIntent>(
                    entity,
                    PendingShotIntent{.received = true,
                                      .targetClientId = it->second.targetClientId,
                                      .targetAnim = it->second.targetAnim});
                pendingShotIntents_.erase(it);
            } else {
                // No matching intent — ensure no stale component lingers.
                registry.remove<PendingShotIntent>(entity);
            }
        }
        GROUP2_PROF_SCOPE("weapon");
        systems::runWeapon(registry, dt, particleEvents, pendingKillEvents, &shotDebugReports);
    }
    {
        GROUP2_PROF_SCOPE("movement");
        systems::runMovement(registry, dt, physics::activeWorld());
    }
    {
        GROUP2_PROF_SCOPE("collision");
        systems::runCollision(registry, dt, physics::activeWorld());
    }
    {
        GROUP2_PROF_SCOPE("explosion");
        systems::runExplosion(registry, particleEvents, pendingKillEvents);
    }
    {
        GROUP2_PROF_SCOPE("playerStatus");
        systems::runPlayerStatus(registry, dt);
    }
    {
        GROUP2_PROF_SCOPE("spawnPointCooldowns");
        systems::runSpawnPointCooldowns(registry, dt);
    }
    {
        GROUP2_PROF_SCOPE("weaponSpawners");
        systems::runWeaponSpawners(registry, dt);
    }

    {
        GROUP2_PROF_SCOPE("match");
        matchController.update(dt, registry, server);
    }

    // Phase 4a: snapshot rate decoupled from tick rate. The registry
    // snapshot is the by far biggest piece of per-tick wire traffic, and
    // remote clients can still smoothly interpolate position over the
    // snapshot interval (Phase 5's RemoteHistory + clientRenderTick will
    // formalise this; today's PreviousPosition lerp covers the simpler
    // case). Events (particles, kills) stay tick-accurate — they're
    // discrete moments and ~1-tick latency materially affects gameplay
    // feel (kill feed lag, missing tracers).
    //
    // Stage 3b's dedicated network thread continuously drains the per-
    // client OutboundQueue to sockets at ~1 kHz, so the game-tick budget
    // no longer pays for the I/O syscalls.
    if ((tickCount % snapshotEveryNTicks) == 0) {
        GROUP2_PROF_SCOPE("broadcastRegistry");
        server.broadcastRegistry(registry);
    }
    {
        GROUP2_PROF_SCOPE("broadcastEvents");
        server.broadcastParticleEvents(particleEvents);
        server.broadcastKillEvents(pendingKillEvents);
    }
    pendingKillEvents.clear();

    // PR-20: serialize each captured shot-debug report and send it
    // unicast to the shooter that produced it.  Wire format defined in
    // `network/ShotDebugReport.hpp`.  Layout:
    //   [PacketType:1][ReportHeader:48][TargetHeader:4 + WireCapsule:32 × N] × numTargets
    //
    // Capped per-tick to avoid pathological cases (a player held LMB
    // on a beam weapon for the whole tick → multiple captures all
    // referencing nearly-identical state).  In practice we see 1-2
    // entries per tick per shooting player.
    if (!shotDebugReports.empty()) {
        GROUP2_PROF_SCOPE("shotDebugSend");
        for (const auto& cap : shotDebugReports) {
            // Reserve worst-case so we only allocate once per shot.
            std::size_t total = 1 /*PacketType*/ + sizeof(net::shotdebug::ReportHeader);
            for (const auto& tgt : cap.targets)
                total +=
                    sizeof(net::shotdebug::TargetHeader) + tgt.capsules.size() * sizeof(net::shotdebug::WireCapsule);
            std::vector<std::uint8_t> bytes;
            bytes.reserve(total);
            bytes.push_back(static_cast<std::uint8_t>(PacketType::SHOT_DEBUG_REPORT));

            net::shotdebug::ReportHeader rh{};
            rh.shotInputTick = cap.shotInputTick;
            rh.hitTargetClientId = cap.hitTargetClientId;
            rh.hitRegion = cap.hitRegion;
            rh.numTargets = static_cast<std::uint8_t>(std::min<std::size_t>(cap.targets.size(), 255));
            rh.originX = cap.origin.x;
            rh.originY = cap.origin.y;
            rh.originZ = cap.origin.z;
            rh.dirX = cap.direction.x;
            rh.dirY = cap.direction.y;
            rh.dirZ = cap.direction.z;
            rh.range = cap.range;
            rh.hitX = cap.hitPoint.x;
            rh.hitY = cap.hitPoint.y;
            rh.hitZ = cap.hitPoint.z;
            const auto* rhBytes = reinterpret_cast<const std::uint8_t*>(&rh);
            bytes.insert(bytes.end(), rhBytes, rhBytes + sizeof(rh));

            for (std::uint8_t i = 0; i < rh.numTargets; ++i) {
                const auto& tgt = cap.targets[i];
                net::shotdebug::TargetHeader th{};
                th.targetClientId = tgt.clientId;
                th.numCapsules = static_cast<std::uint8_t>(std::min<std::size_t>(tgt.capsules.size(), 255));
                const auto* thBytes = reinterpret_cast<const std::uint8_t*>(&th);
                bytes.insert(bytes.end(), thBytes, thBytes + sizeof(th));
                for (std::uint8_t c = 0; c < th.numCapsules; ++c) {
                    const auto& src = tgt.capsules[c];
                    net::shotdebug::WireCapsule wc{};
                    wc.pointAx = src.pointA.x;
                    wc.pointAy = src.pointA.y;
                    wc.pointAz = src.pointA.z;
                    wc.pointBx = src.pointB.x;
                    wc.pointBy = src.pointB.y;
                    wc.pointBz = src.pointB.z;
                    wc.radius = src.radius;
                    wc.region = static_cast<std::uint8_t>(src.region);
                    const auto* wcBytes = reinterpret_cast<const std::uint8_t*>(&wc);
                    bytes.insert(bytes.end(), wcBytes, wcBytes + sizeof(wc));
                }
            }
            // replaceKey 0 = always append, never drop on age.  These
            // are diagnostic packets — losing one is fine, but if the
            // queue happened to coalesce them by key the user would
            // see only the most-recent shot in the ring buffer.
            server.sendToClient(ClientId{cap.shooterClientId}, bytes.data(), static_cast<int>(bytes.size()));
        }
    }

    ++tickCount;

    // PR-18: ground-truth log for offline desync analysis.  Throttled
    // sample of the server-authoritative state per replicated player.
    // Cheap (~150 KB/s at 100 bots × 32 Hz) and silent when the env
    // var isn't set.  Lives outside the tick-end profiler scope below
    // so its I/O isn't attributed to any single ECS scope.
    writeGroundTruthLogIfDue();

    // Record the tick's total wall time for the 1 Hz aggregator. Cheap:
    // a single atomic increment + min/max + histogram bucket bump.
    const Uint64 tickEndCounter = SDL_GetPerformanceCounter();
    ::group2::perf::tickEnd(::group2::perf::ticksToNs(tickEndCounter - tickStartCounter));

    // Log once per second so we can watch the test entity fall and land.

    // if (tickCount % tickRateHz == 0) {
    //     registry.view<Position>().each([this](const Position& pos) {
    //         SDL_Log("[server] tick %d | pos (%.1f, %.1f, %.1f)",
    //                 tickCount,
    //                 static_cast<double>(pos.value.x),
    //                 static_cast<double>(pos.value.y),
    //                 static_cast<double>(pos.value.z));
    //     });
    // }
}

void ServerGame::initNewPlayerEntity(ClientId clientId)
{
    const entt::entity player = registry.create();
    clientEntities[clientId] = player;

    registry.emplace<Player>(player, Player{});
    registry.emplace<ClientId>(player, clientId);
    registry.emplace<InputSnapshot>(player);
    registry.emplace<Position>(player, glm::vec3{0.0f, 200.0f, 0.0f});
    registry.emplace<Velocity>(player);
    registry.emplace<CollisionShape>(player);
    registry.emplace<PlayerVisState>(player);
    registry.emplace<PlayerSimState>(player);
    registry.emplace<Renderable>(player, Renderable{.modelIndex = 1, .scale = glm::vec3(100.0f)});
    registry.emplace<Health>(player, Health{}); // Defaults to 100/100 health and 100/100 armor
    registry.emplace<PlayerMatchStats>(player, PlayerMatchStats{});
    registry.emplace<BeamState>(player);

    const WeaponConfig& rifleConfig = getWeaponConfig(WeaponType::Rifle);
    const WeaponConfig& railConfig = getWeaponConfig(WeaponType::RailGun);
    registry.emplace<WeaponState>(player,
                                  WeaponState{
                                      .primary =
                                          GunInstance{
                                              .type = WeaponType::Rifle,
                                              .totalAmmo = rifleConfig.defaultAmmoCapacity,
                                              .currentMagAmmo = rifleConfig.magazineSize,
                                              .fireCooldown = 0.0f,
                                          },
                                      .secondary =
                                          GunInstance{
                                              .type = WeaponType::RailGun,
                                              .totalAmmo = railConfig.defaultAmmoCapacity,
                                              .currentMagAmmo = railConfig.magazineSize,
                                              .fireCooldown = 0.0f,
                                          },
                                      .current = WeaponSlot::PRIMARY,
                                  });

    // Attach server-side animator for skeleton-driven hitboxes.
    attachServerAnimator(player);

    SDL_Log("[server] spawned player entity for client %d", clientId.value);
}

void ServerGame::deletePlayerEntity(ClientId clientId)
{
    if (const auto it = clientEntities.find(clientId); it != clientEntities.end()) {
        const entt::entity player = it->second;
        detachServerAnimator(player);
        if (registry.valid(player)) {
            registry.destroy(player);
        }
        clientEntities.erase(it);
    }
}

// Server-side animation subsystem

void ServerGame::initAnimation()
{
    const char* base = SDL_GetBasePath();
    const std::string assetsDir = std::string(base ? base : "") + "assets/animations/";
    const std::string rigPath = assetsDir + "standard_walk.fbx";

    if (!serverRig_.loadFromFBX(rigPath)) {
        SDL_Log("[server] WARNING: rig load failed — skeleton hitboxes disabled, falling back to AABB");
        return;
    }

    SDL_Log("[server] rig loaded — %d joints, %zu mesh(es)", serverRig_.numJoints(), serverRig_.meshes().size());

    // Auto-calculate rig scale (same logic as client).
    {
        float meshMinY = 0.0f;
        float meshMaxY = 1.0f;
        serverRig_.verticalBounds(meshMinY, meshMaxY);
        rigMeshMinY_ = meshMinY;

        const float meshHeight = meshMaxY - meshMinY;
        const float targetHeight = 2.0f * tms::k_standingHalfHeight; // 72 units
        if (meshHeight > 0.001f) {
            rigScale_ = targetHeight / meshHeight;
        } else {
            rigScale_ = 1.0f;
        }
        SDL_Log("[server] rig auto-scale: meshY=[%.1f, %.1f] height=%.1f -> scale=%.4f",
                static_cast<double>(meshMinY),
                static_cast<double>(meshMaxY),
                static_cast<double>(meshHeight),
                static_cast<double>(rigScale_));
    }

    // Load animation clips.
    for (uint8_t i = 0; i < static_cast<uint8_t>(ClipId::_Count); ++i) {
        const ClipId id = static_cast<ClipId>(i);
        const std::string clipPath = assetsDir + clipFile(id);
        if (!serverAnimLibrary_.loadClipFromFBX(serverRig_, id, clipPath)) {
            SDL_Log("[server] WARNING: failed to load clip '%s'", clipName(id));
            continue;
        }
    }

    // Build and resolve hitbox definitions.
    hitboxRig_ = HitboxRig::buildMixamoDefault();
    hitboxRig_.resolveIndices(serverRig_.jointMap());

    int resolved = 0;
    for (const auto& def : hitboxRig_.definitions)
        if (def.boneIndex >= 0)
            ++resolved;

    SDL_Log("[server] hitbox rig: %zu definitions, %d resolved", hitboxRig_.definitions.size(), resolved);
    animationLoaded_ = true;
}

void ServerGame::attachServerAnimator(entt::entity player)
{
    if (!animationLoaded_)
        return;

    auto animator = std::make_unique<CharacterAnimator>(serverRig_, serverAnimLibrary_);
    // No skinning backend needed on server — we only read joint matrices.
    serverAnimators_[player] = std::move(animator);
}

void ServerGame::detachServerAnimator(entt::entity player)
{
    serverAnimators_.erase(player);
}

void ServerGame::updateLagCompTargets()
{
    // PR-20.7 (root-cause fix): rewind = RTT + cl_interp, NOT
    // RTT/2 + cl_interp.
    //
    // Pre-PR-20.7 we used the Source-engine formula `RTT/2 + interp`,
    // which is correct ONLY when the client predicts other players
    // forward to its estimate of server-now (so the client renders
    // enemies at `server_now − cl_interp`).  Our client does not do
    // that prediction — it renders at `most_recent_snapshot_apply −
    // cl_interp`.  The most-recent snapshot was generated at
    // `server_now − inbound_RTT/2`, so our client actually renders
    // enemies at `server_now − RTT/2 − cl_interp`.
    //
    // Concrete derivation (T = client clock time of fire, sync'd to
    // server clock):
    //   client renders enemy at server time   T − RTT/2 − cl_interp
    //   server processes input at server time T + RTT/2  (outbound)
    //   server should rewind to exactly       T − RTT/2 − cl_interp
    //   rewind amount = (T + RTT/2) − (T − RTT/2 − cl_interp)
    //                 = RTT + cl_interp
    //
    // Symptom of the under-rewind at 100 ms simulated RTT: red
    // (server-rewound) capsule was ~50 ms (i.e. RTT/2) AHEAD of blue
    // (client-rendered) capsule in the shot-debug visualizer.  At
    // 400 u/s enemy speed that's ~20 units of visible separation —
    // exactly what the user reported.  Bumping the rewind to
    // `rttTicks + interpDelayTicks` collapses that gap to
    // sub-tick / quantization noise.
    //
    // Cap on total rewind depth.  Worst case at the limits:
    //   RTT 200 ms = 25.6 ticks  (PR-12 simulator cap)
    //   interp 8 snapshots × 1 tick @ 128 Hz = 8 ticks
    //   total = ~34 ticks (~265 ms).
    // 64 ticks (500 ms) gives plenty of headroom.  HitboxHistory
    // capacity is also 64; both cap and history match.
    static constexpr uint32_t k_maxLagCompTicks = 64;

    // PR-2b + PR-12: snapshot every client's net state (RTT + interp
    // delay) in one mostly-lock-free operation, then drive the loop
    // off the local copy.
    static thread_local std::vector<Server::ClientNetState> netCache;
    server.snapshotClientNetStates(netCache);

    // Lookup map from cache for the inner loop. Reserve once, reuse
    // the std::unordered_map across ticks — avoids per-tick alloc.
    struct NetCacheEntry
    {
        uint16_t rttMs;
        uint8_t interpDelaySnapshots;
    };
    static thread_local std::unordered_map<ClientId, NetCacheEntry> netById;
    netById.clear();
    netById.reserve(netCache.size());
    for (const auto& s : netCache)
        netById.emplace(s.id, NetCacheEntry{.rttMs = s.rttMs, .interpDelaySnapshots = s.interpDelaySnapshots});

    // Snapshot interval in physics ticks — used to convert the
    // client's interpDelaySnapshots into a tick count.  Read from
    // ServerGame's runtime config so it tracks any future env-var
    // adjustment (e.g. PR-13 dropping snapshotEveryNTicks 4 → 1).
    const auto snapshotEveryNTicksLocal = static_cast<uint32_t>(std::max(1, snapshotEveryNTicks));

    const auto currentServerTick = static_cast<uint32_t>(tickCount);
    for (const auto& [clientId, entity] : clientEntities) {
        if (!registry.valid(entity))
            continue;

        const auto netIt = netById.find(clientId);
        const uint16_t rttMs = (netIt != netById.end()) ? netIt->second.rttMs : 0;
        const uint8_t interpDelaySnapshots = (netIt != netById.end()) ? netIt->second.interpDelaySnapshots : 0;
        // PR-20.7: full-RTT (NOT half-RTT) in ticks.  Round-to-nearest
        // via integer-only `(rttMs * Hz + 500) / 1000`.  See the long
        // header comment on this function for the derivation; in
        // short, our client renders remote players at
        // `most_recent_snapshot_apply − cl_interp` rather than
        // predicting them forward to estimated server-now, so the
        // rewind has to absorb both the inbound and outbound legs of
        // the RTT — not just the outbound half.
        const uint32_t rttTicks = (static_cast<uint32_t>(rttMs) * static_cast<uint32_t>(tickRateHz) + 500u) / 1000u;

        // PR-12: client render-delay term.  The client renders remote
        // entities at `most_recent_apply − interpDelaySnapshots ×
        // snapshotInterval`; the rewind subtracts this on top of the
        // full-RTT term so the server lands on exactly the historical
        // capsule state the client SAW on screen at fire time.
        const uint32_t interpDelayTicks = static_cast<uint32_t>(interpDelaySnapshots) * snapshotEveryNTicksLocal;

        const uint32_t lagTicks = std::min<uint32_t>(rttTicks + interpDelayTicks, k_maxLagCompTicks);
        const uint32_t targetTick = (lagTicks == 0 || lagTicks >= currentServerTick)
                                        ? 0u // explicit "no rewind" sentinel
                                        : (currentServerTick - lagTicks);

        // PR-22: stash `lagTicks` and `rttMs` alongside `targetServerTick`
        // so the shot-log can record them per-shot without plumbing
        // `tickCount` and `netById` into `WeaponSystem::handleFire`.
        // The rewinder ignores these fields; they're informational.
        registry.emplace_or_replace<LagCompTarget>(
            entity,
            LagCompTarget{
                .targetServerTick = targetTick,
                .lagTicks = static_cast<uint16_t>(std::min<uint32_t>(lagTicks, 0xFFFFu)),
                .rttMs = rttMs,
            });

        // Replicate this client's RTT to all clients via PlayerMatchStats.
        // Already in the Synced tuple, so ships in the next snapshot. Each
        // client's scoreboard HUD then reads the target row's `rttMs`
        // instead of falling back to its own `NetworkStats::rttMs` for
        // every row (which would mean every player on screen showed the
        // *local* client's ping). Costs one uint16 per replicated player
        // per snapshot — well under any other field in the component.
        if (auto* stats = registry.try_get<PlayerMatchStats>(entity))
            stats->rttMs = rttMs;
    }
}

void ServerGame::updateAnimationAndHitboxes(float dt)
{
    if (!animationLoaded_)
        return;

    // PR-3 (server-perf): split the per-animator update into a fan-out
    // parallel kernel. The work is embarrassingly parallel across
    // players — each animator reads only its own entity's components
    // (read-only) and writes only its own JointMatrices slot. The
    // single piece of shared state we have to handle is `entt::registry`
    // itself: `get_or_emplace<JointMatrices>` may grow the underlying
    // pool if the slot doesn't exist yet, and pool growth is NOT
    // thread-safe.
    //
    // Pre-pass: ensure every valid animator entity has a JointMatrices
    // component (sequential). Then the parallel pass only writes into
    // already-allocated slots, which is safe — entt component pools
    // tolerate concurrent writes to *distinct* entities once allocated.

    // Phase 1a (sequential): pre-emplace JointMatrices and collect the
    // work items. Reusing the static thread_local vector across ticks
    // avoids per-tick allocation.
    static thread_local std::vector<std::pair<entt::entity, CharacterAnimator*>> work;
    work.clear();
    work.reserve(serverAnimators_.size());
    for (auto& [entity, animator] : serverAnimators_) {
        if (!registry.valid(entity))
            continue;
        // Alloc-if-needed, single-threaded. The return reference is
        // intentionally discarded — we only care about the side-effect
        // of guaranteeing the slot exists before the parallel pass.
        (void)registry.get_or_emplace<JointMatrices>(entity);
        work.emplace_back(entity, animator.get());
    }

    // Phase 1b (parallel): each animator updates and writes its own
    // pre-emplaced JointMatrices slot.
    ::group2::perf::parallelFor(work.begin(), work.end(), [&](const auto& item) {
        const entt::entity entity = item.first;
        CharacterAnimator* animator = item.second;

        // Build AnimationInputs from ECS components (same as client).
        AnimationInputs ai{};
        if (const auto* vel = registry.try_get<Velocity>(entity))
            ai.velocityWorld = vel->value;
        if (const auto* inp = registry.try_get<InputSnapshot>(entity)) {
            ai.yawRad = inp->yaw;
            ai.pitchRad = inp->pitch;
        }
        if (const auto* ps = registry.try_get<PlayerVisState>(entity)) {
            ai.grounded = ps->grounded;
            ai.sprinting = ps->sprinting;
            ai.crouching = ps->crouching;
            ai.moveMode = static_cast<int>(ps->moveMode);
            ai.wallRunSide = static_cast<int>(ps->wallRunSide);
        }

        animator->update(ai, dt);

        // Slot exists (Phase 1a); just write the matrices.
        registry.get<JointMatrices>(entity).matrices = animator->jointModelMatrices();

        // PR-27 (netsync): mirror the animator's sampler array into an
        // `AnimSnapshot` ECS component.  HitboxHistorySystem reads it
        // the same tick to seed each ring slot's `anim` field, so the
        // shot-resolution path can compare client-claimed vs server-
        // historical animation state.  Cheap copy: 5 slots × 9 bytes.
        auto& snap = registry.get_or_emplace<AnimSnapshot>(entity);
        const auto& samplers = animator->samplers();
        for (std::size_t i = 0; i < AnimSnapshot::k_numSlots && i < samplers.size(); ++i) {
            const auto& src = samplers[i];
            const bool active = src.active && src.weight > 0.0f;
            auto& dst = snap.slots[i];
            dst.clipIdRaw = active ? static_cast<std::uint8_t>(src.id) : 0xFFu;
            dst.timeRatio = active ? src.timeRatio : 0.0f;
            dst.weight = active ? src.weight : 0.0f;
        }
    });

    // Step 2: Transform bone poses into world-space hitbox capsules.
    // updateHitboxes itself was parallelized in PR-3; see HitboxSystem.cpp.
    systems::updateHitboxes(registry, hitboxRig_, rigScale_, rigMeshMinY_);
}
