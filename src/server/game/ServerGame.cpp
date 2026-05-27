/// @file ServerGame.cpp
/// @brief Implementation of the server-side game loop, tick logic, and player management.

#include "ServerGame.hpp"

#include "client/animation/CharacterAnimator.hpp"
#include "ecs/AssetCatalog.hpp"
#include "ecs/MapConfig.hpp"
#include "ecs/abilities/DashAbility.hpp"
#include "ecs/abilities/GrappleAbility.hpp"
#include "ecs/abilities/GravityAbility.hpp"
#include "ecs/abilities/RecallAbility.hpp"
#include "ecs/components/AbilityState.hpp"
#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/GrenadeState.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LagCompTarget.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerColor.hpp"
#include "ecs/components/PlayerColors.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerName.hpp"
#include "ecs/components/PlayerNicknames.hpp"
#include "ecs/components/PlayerSimState.hpp" // also pulls in PlayerVisState
#include "ecs/components/Position.hpp"
#include "ecs/components/PowerupSpawner.hpp"
#include "ecs/components/PowerupState.hpp"
#include "ecs/components/Renderable.hpp"
#include "ecs/components/RespawnPoint.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/CollisionEvents.hpp"
#include "ecs/physics/PhaseDiagnostic.hpp"
#include "ecs/physics/Sleep.hpp"
#include "ecs/physics/Solver.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/systems/AbilitySystem.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/DroppedWeaponSystem.hpp"
#include "ecs/systems/DynamicsSystem.hpp"
#include "ecs/systems/ExplosionSystem.hpp"
#include "ecs/systems/FireSystem.hpp"
#include "ecs/systems/HitboxSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "ecs/systems/PowerupSpawnerSystem.hpp"
#include "ecs/systems/PowerupSystem.hpp"
#include "ecs/systems/RagdollSystem.hpp"
#include "ecs/systems/TriggerSystem.hpp"
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
#include <cstddef>
#include <cstdlib>
#include <glm/geometric.hpp>

namespace
{
constexpr float k_lobbyStartCountdownDuration = 3.0f;
constexpr float k_voiceMaxRange = 3500.0f;

template <std::size_t N>
std::vector<AbilityType> chooseTwoAbilities(const std::array<AbilityType, N>& pool)
{
    std::vector<AbilityType> choices(pool.begin(), pool.end());
    if (choices.size() <= kAbilityChoicesPerTier) {
        return choices;
    }

    std::vector<AbilityType> selected;
    selected.reserve(kAbilityChoicesPerTier);
    for (std::size_t i = 0; i < kAbilityChoicesPerTier; ++i) {
        const auto idx = static_cast<std::size_t>(std::rand()) % choices.size();
        selected.push_back(choices[idx]);
        choices.erase(choices.begin() + static_cast<std::ptrdiff_t>(idx));
    }
    return selected;
}
} // namespace

bool ServerGame::init(Server& serverRef, int hz, int snapshotHz, bool skipLobby)
{
    server = &serverRef;
    tickRateHz = hz;
    matchController.setSkipLobby(skipLobby);

    // Phase 4a: snapshot rate ≤ tick rate. Both should be positive ints; we
    // re-clamp here in case the caller didn't (NetworkConfig already
    // clamps the loaded value to [1, 256]).
    const int clampedSnapshotHz = std::max(1, std::min(snapshotHz, hz));
    snapshotEveryNTicks = std::max(1, hz / clampedSnapshotHz);
    SDL_Log("[server] tickRate=%d Hz, snapshotRate≈%d Hz (every %d ticks)",
            hz,
            hz / snapshotEveryNTicks,
            snapshotEveryNTicks);

    // Phase-through diagnostic is intentionally opt-in. It writes multiple
    // CSV rows per player per tick and flushes them for crash-safe debugging,
    // which is far too expensive for normal servers.
    physics::diag::setFilePrefix("server");
    const char* phaseDiagEnv = std::getenv("GROUP2_PHASE_DIAG");
    const bool phaseDiagEnabled = phaseDiagEnv != nullptr && phaseDiagEnv[0] != '\0' && phaseDiagEnv[0] != '0';
    physics::diag::setEnabled(phaseDiagEnabled);
    SDL_Log("[server] phase-through diagnostic %s", phaseDiagEnabled ? "ENABLED" : "disabled");

    const char* shotDebugEnv = std::getenv("GROUP2_SHOT_DEBUG");
    shotDebugEnabled_ = shotDebugEnv != nullptr && shotDebugEnv[0] != '\0' && shotDebugEnv[0] != '0';
    SDL_Log("[server] shot debug reports %s", shotDebugEnabled_ ? "ENABLED" : "disabled");

    clientEntities.clear(); // For safety
    registry.clear();
    if (!lobbyManager.init(serverRef)) {
        SDL_Log("[server] LobbyManager init failed");
        return false;
    }

    // Register abilities
    abilityRegistry.registerAbility(std::make_unique<DashAbility>());
    abilityRegistry.registerAbility(std::make_unique<GrappleAbility>());
    abilityRegistry.registerAbility(std::make_unique<GravityAbility>());
    abilityRegistry.registerAbility(std::make_unique<RecallAbility>());

    // ── Load map collision ──────────────────────────────────────────────
    // Map filename and load-mode toggles live in ecs/MapConfig.hpp so the
    // client and server load the exact same primitives (a prerequisite for
    // prediction parity).  To switch maps, edit `kMapAsset` in AssetCatalog.hpp.
    // To change *how* the map is loaded, edit the constants in MapConfig.hpp.
    {
        gamemap::loadConfiguredMap(mapCollision_, "server");

        // Load prop collision — must match client for prediction parity.
        // Non-convex props fall back to triMesh in normal builds. Legacy V-HACD
        // only runs when both the asset/config opt in and CMake enables
        // GROUP2_ENABLE_VHACD.
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

    // Weapon spawners
    for (const gamemap::WeaponSpawner& weaponSpawner : gamemap::weaponSpawner_) {
        WeaponType weaponType = weaponSpawner.type;
        glm::vec3 pos = weaponSpawner.pos;

        const entt::entity spawner = registry.create();
        registry.emplace<WeaponSpawner>(
            spawner,
            WeaponSpawner{.type = weaponType, .spawnCooldown = systems::weaponCooldownTime, .hasWeapon = true});
        CollisionShape shape{.halfExtents = {32.0f, 32.0f, 32.0f}};
        glm::vec3 centeredPos = pos + glm::vec3{0.0f, shape.halfExtents.y, 0.0f};

        registry.emplace<Position>(spawner, centeredPos);
        registry.emplace<CollisionShape>(spawner, shape);
    }

    // Respawn points (with cooldown state)
    for (const glm::vec3& spawnPos : gamemap::spawnPoints_) {
        const entt::entity spawnPoint = registry.create();
        registry.emplace<RespawnPoint>(spawnPoint, RespawnPoint{});
        registry.emplace<Position>(spawnPoint, spawnPos);
    }

    // Powerup spawners
    for (const gamemap::PowerupSpawner& powerupSpawner : gamemap::powerupSpawner_) {
        PowerupType powerupType = powerupSpawner.type;
        glm::vec3 pos = powerupSpawner.pos;

        PowerupConfig config = getPowerupConfig(powerupType);

        const entt::entity spawner = registry.create();
        registry.emplace<PowerupSpawner>(
            spawner, PowerupSpawner{.type = config.type, .spawnCooldown = config.spawnCooldown, .hasPowerup = false});
        CollisionShape shape{.halfExtents = {32.0f, 32.0f, 32.0f}};
        glm::vec3 centeredPos = pos + glm::vec3{0.0f, shape.halfExtents.y, 0.0f};

        registry.emplace<Position>(spawner, centeredPos);
        registry.emplace<CollisionShape>(spawner, shape);
        registry.emplace<CollisionShape>(spawner);
    }

    while (running) {
        server->poll();

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

void ServerGame::applyInputEvent(ClientId clientId, const InputSnapshot& inputSnapshot)
{
    GROUP2_PROF_SCOPE("eventInput");

    const auto entityIt = clientEntities.find(clientId);
    if (entityIt == clientEntities.end())
        return;

    const entt::entity player = entityIt->second;
    if (!registry.valid(player))
        return;

    InputSnapshot& input = registry.get_or_emplace<InputSnapshot>(player);
    input = inputSnapshot;
}

void ServerGame::eventHandler(const Event& event)
{
    switch (event.type) {
    case EventType::Connected: {
        GROUP2_PROF_SCOPE("eventConnected");
        initNewPlayerEntity(event.clientId);
        const bool sent = server->notifyPlayerClientId(event.clientId, clientEntities[event.clientId]);
        if (!sent) {
            deletePlayerEntity(event.clientId);
            break;
        }
        lobbyManager.addPlayer(event.clientId);
        server->sendMatchConfigToClient(event.clientId, matchController.getMatchConfig());
        break;
    }
    case EventType::Disconnected: {
        GROUP2_PROF_SCOPE("eventDisconnected");
        lobbyManager.removePlayer(event.clientId);
        deletePlayerEntity(event.clientId);
        break;
    }
    case EventType::Input: {
        applyInputEvent(event.clientId, event.movementIntent);
        break;
    }
    case EventType::PlayerReady: {
        GROUP2_PROF_SCOPE("eventLobby");
        lobbyManager.setPlayerReadyStatus(event.clientId, true);
        break;
    }
    case EventType::PlayerUnready: {
        GROUP2_PROF_SCOPE("eventLobby");
        lobbyManager.setPlayerReadyStatus(event.clientId, false);
        break;
    }
    case EventType::StartMatchRequested: {
        GROUP2_PROF_SCOPE("eventLobby");
        if (!lobbyStartCountdownActive && lobbyManager.hostStartMatch(event.clientId)) {
            lobbyStartCountdownActive = true;
            lobbyStartCountdownTimer = k_lobbyStartCountdownDuration;
            lobbyStartRequester = event.clientId;
            server->broadcastMatchStatus(MatchStatePacket{
                .phase = MatchPhase::LOBBY,
                .countdownTimer = lobbyStartCountdownTimer,
                .winnerId = -1,
            });
        }
        break;
    }
    case EventType::ShotIntent: {
        GROUP2_PROF_SCOPE("eventShotIntent");
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
    case EventType::PhysicsDiagRecording: {
        GROUP2_PROF_SCOPE("eventPhysicsDiagRecording");
        if (event.physicsDiagRecording) {
            physics::diag::startRecording();
            SDL_Log("[server] physics CSV recording STARTED by client %d", event.clientId.value);
        } else {
            physics::diag::stopRecording();
            SDL_Log("[server] physics CSV recording stopped by client %d", event.clientId.value);
        }
        break;
    }
    case EventType::TextChat: {
        GROUP2_PROF_SCOPE("eventTextChat");
        const auto entityIt = clientEntities.find(event.clientId);
        if (entityIt == clientEntities.end() || !registry.valid(entityIt->second))
            return;
        server->broadcastTextChat(event.clientId, event.textChat.message);
        break;
    }
    case EventType::VoiceFrame: {
        GROUP2_PROF_SCOPE("eventVoice");
        const auto speakerIt = clientEntities.find(event.clientId);
        if (speakerIt == clientEntities.end() || !registry.valid(speakerIt->second))
            return;
        const Position* speakerPos = registry.try_get<Position>(speakerIt->second);
        if (!speakerPos)
            return;

        constexpr float maxRangeSq = k_voiceMaxRange * k_voiceMaxRange;
        static thread_local std::vector<ClientId> recipients;
        recipients.clear();
        recipients.reserve(clientEntities.size());
        for (const auto& [listenerClientId, listenerEntity] : clientEntities) {
            if (listenerClientId == event.clientId || !registry.valid(listenerEntity))
                continue;
            const Position* listenerPos = registry.try_get<Position>(listenerEntity);
            if (!listenerPos)
                continue;
            const glm::vec3 delta = listenerPos->value - speakerPos->value;
            if (glm::dot(delta, delta) > maxRangeSq)
                continue;
            recipients.push_back(listenerClientId);
        }
        server->sendVoiceFrameToClients(
            recipients, event.clientId, event.voiceFrame.sequence, event.voiceFrame.frameMs, event.voiceFrame.opus);
        break;
    }
    case EventType::MatchConfigUpdated: {
        GROUP2_PROF_SCOPE("eventMatchConfigUpdated");
        // Client can propose new match config (e.g. kill threshold) which is then broadcast to all clients.
        if (isHost(event.clientId) && matchController.setMatchConfig(event.matchConfig)) {
            server->broadcastMatchConfig(matchController.getMatchConfig());
        }
        break;
    }
    case EventType::ServerShutdownRequested: {
        GROUP2_PROF_SCOPE("eventServerShutdownRequested");
        if (!isHost(event.clientId)) {
            SDL_Log("ServerGame: rejecting shutdown request from non-host clientId %u", event.clientId.value);
            break;
        }

        SDL_Log("ServerGame: host clientId %u requested server shutdown", event.clientId.value);
        shutdown();
        break;
    }
    default:
        break;
    }
}

void ServerGame::selectMatchAbilityPool()
{
    matchPrimaryAbilities = chooseTwoAbilities(primaryAbilityTypes);
    matchSecondaryAbilities = chooseTwoAbilities(secondaryAbilityTypes);

    registry.view<Player, AbilityState>().each([this](AbilityState& abilityState) {
        abilityState = AbilityState{};
        applyMatchAbilityChoices(abilityState);
    });
}

void ServerGame::applyMatchAbilityChoices(AbilityState& state) const
{
    for (std::size_t i = 0; i < kAbilityChoicesPerTier; ++i) {
        state.primaryChoices[i] =
            (i < matchPrimaryAbilities.size()) ? matchPrimaryAbilities[i] : primaryAbilityTypes[i];
        state.secondaryChoices[i] =
            (i < matchSecondaryAbilities.size()) ? matchSecondaryAbilities[i] : secondaryAbilityTypes[i];
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
        static thread_local std::unordered_map<ClientId, InputSnapshot> latestInputs;
        server->drainEvents(events);
        latestInputs.clear();
        latestInputs.reserve(clientEntities.size());

        std::size_t processedEvents = 0;
        std::size_t voiceEvents = 0;
        std::size_t chatEvents = 0;
        bool exceededEventBudget = false;
        for (const Event& event : events) {
            if (event.type == EventType::Input) {
                latestInputs[event.clientId] = event.movementIntent;
                continue;
            }
            if (event.type == EventType::VoiceFrame)
                ++voiceEvents;
            else if (event.type == EventType::TextChat)
                ++chatEvents;
            eventHandler(event);
            ++processedEvents;
            // Tick-time bail-out kept identical to pre-PR-2b: if event
            // processing alone blows the tick budget we abort the rest
            // of the events (lost — TODO upstream the drop reason).
            if (const Uint64 kNow = SDL_GetPerformanceCounter(); kNow >= nextTick) {
                exceededEventBudget = true;
                break;
            }
        }
        for (const auto& [clientId, inputSnapshot] : latestInputs) {
            applyInputEvent(clientId, inputSnapshot);
            ++processedEvents;
        }
        if (exceededEventBudget) {
            static Uint64 nextWarningMs = 0;
            const Uint64 nowMs = SDL_GetTicks();
            if (nowMs >= nextWarningMs) {
                SDL_Log("[server] Exceeded tick time for event handling: drained=%zu processed=%zu coalescedInputs=%zu "
                        "voice=%zu chat=%zu",
                        events.size(),
                        processedEvents,
                        latestInputs.size(),
                        voiceEvents,
                        chatEvents);
                nextWarningMs = nowMs + 1000;
            }
        }
    }

    // Check if idle server should shutdown
    GROUP2_PROF_SCOPE("idleCheck");
    if (idleShutdownEnabled_) {
        if (server->getClientCount() > 0) {
            lastNonEmptyMs_ = SDL_GetTicks();
        } else if (idleShutdownMs_ > 0 && (SDL_GetTicks() - lastNonEmptyMs_) >= idleShutdownMs_) {
            SDL_Log("[server] Idle shutdown triggered after %d minutes with no clients",
                    static_cast<int>(idleShutdownMs_ / 60000));
            running = false;
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
    std::vector<net::shotdebug::ShotDebugCapture>* shotDebugSink = shotDebugEnabled_ ? &shotDebugReports : nullptr;
    {
        // PR-27: stash pending SHOT_INTENTs onto each shooter as a
        // transient `PendingShotIntent` component, keyed by the
        // shooter's CURRENT input tick.  `WeaponSystem::handleFire`
        // reads this when logging the shot to record the client-
        // asserted target id + anim state delta in `servershots.csv`.
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
        systems::runWeapon(registry, dt, particleEvents, pendingKillEvents, shotDebugSink);
    }
    {
        GROUP2_PROF_SCOPE("ability");
        systems::runAbility(registry, abilityRegistry, dt);
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
        // Phase 4: trigger overlap diff → Enter / Stay / Exit events.
        // Drained by gameplay below.  Server is authoritative; clients pass
        // isPredictedClient=true so their predicted ticks don't double-fire.
        GROUP2_PROF_SCOPE("triggers");
        physics::events::beginTick();
        systems::runTriggers(registry, /*isPredictedClient=*/false);
    }
    {
        // Phase 6/10/12 dynamics: integrate, solve contacts + joints,
        // update sleep state for every entity with a RigidBody.  Player
        // movement is still kinematic (CollisionSystem above); this tick
        // exists for ragdoll bones + future dynamic props.
        GROUP2_PROF_SCOPE("dynamics");
        static const physics::SolverConfig k_solverCfg{};
        static const physics::SleepConfig k_sleepCfg{};
        systems::runDynamics(registry, dt, physics::activeWorld(), contactCache_, k_solverCfg, k_sleepCfg);
    }
    {
        // Phase 13: age out ragdolls so gameplay can fade / despawn corpses.
        GROUP2_PROF_SCOPE("ragdolls");
        systems::runRagdolls(registry, dt);
    }
    {
        GROUP2_PROF_SCOPE("explosion");
        systems::runExplosion(registry, particleEvents, pendingKillEvents);
    }
    {
        GROUP2_PROF_SCOPE("fireField");
        systems::runFireField(registry, dt, pendingKillEvents);
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
        GROUP2_PROF_SCOPE("droppedWeapons");
        systems::runDroppedWeapons(registry, dt);
    }
    {
        GROUP2_PROF_SCOPE("PowerupSpawners");
        systems::runPowerupSpawners(registry, dt);
    }
    {
        GROUP2_PROF_SCOPE("powerup");
        systems::runPowerups(registry, dt);
    }

    {
        GROUP2_PROF_SCOPE("match");
        if (lobbyStartCountdownActive) {
            lobbyStartCountdownTimer -= dt;
            if (lobbyStartCountdownTimer <= 0.0f) {
                lobbyStartCountdownActive = false;
                lobbyStartCountdownTimer = 0.0f;
                if (lobbyManager.hostStartMatch(lobbyStartRequester)) {
                    selectMatchAbilityPool();
                    matchController.hostStartedMatch();
                    matchController.update(dt, registry, *server);
                } else {
                    server->broadcastMatchStatus(MatchStatePacket{
                        .phase = MatchPhase::LOBBY,
                        .countdownTimer = 0.0f,
                        .winnerId = -1,
                    });
                }
            }
        } else {
            const MatchPhase previousPhase = matchController.getCurrentPhase();
            matchController.update(dt, registry, *server);
            if (previousPhase != MatchPhase::COUNTDOWN && matchController.getCurrentPhase() == MatchPhase::COUNTDOWN)
                selectMatchAbilityPool();
            if (previousPhase != MatchPhase::LOBBY && matchController.getCurrentPhase() == MatchPhase::LOBBY)
                lobbyManager.resetReadyStatuses();
        }
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
        server->broadcastRegistry(registry);
    }
    {
        GROUP2_PROF_SCOPE("broadcastEvents");
        server->broadcastParticleEvents(particleEvents);
        server->broadcastKillEvents(pendingKillEvents);
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
    if (shotDebugSink != nullptr && !shotDebugReports.empty()) {
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
            server->sendToClient(ClientId{cap.shooterClientId}, bytes.data(), static_cast<int>(bytes.size()));
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
    // Phase 5: player is a capsule for smoother movement against ramps and
    // triangulated geometry.  `halfExtents` is the capsule's tight bounding
    // box (32×72×32) so existing axis-aligned swept queries treat the shape
    // exactly; the capsule fields drive Phase-5+ shape-aware paths.
    // Attach CollisionShape before resolving the spawn position so the
    // recovery sweep uses the player's actual capsule.
    registry.emplace<CollisionShape>(player,
                                     CollisionShape{
                                         .type = CollisionShapeType::Capsule,
                                         .halfExtents = {16.0f, 36.0f, 16.0f},
                                         .radius = 16.0f,
                                         .halfHeight = 20.0f,
                                     });
    registry.emplace<Position>(player, systems::chooseAndResolveSpawnPosition(registry, player));
    registry.emplace<Velocity>(player);
    registry.emplace<PlayerVisState>(player);
    registry.emplace<PlayerSimState>(player);
    registry.emplace<Renderable>(player, Renderable{.modelIndex = 1, .scale = glm::vec3(100.0f)});
    registry.emplace<Health>(player, Health{}); // Defaults to 100/100 health and 100/100 armor
    registry.emplace<PlayerMatchStats>(player, PlayerMatchStats{});
    AbilityState abilityState{};
    applyMatchAbilityChoices(abilityState);
    registry.emplace<AbilityState>(player, abilityState); // Level 0; level-ups unlock the two ability picks.
    registry.emplace<PowerupState>(player);

    if constexpr (player_colors::k_enabled) {
        // Pick the least-used palette slot; ties broken by lowest index
        // for deterministic assignment across reconnects within a match.
        const auto minIt = std::min_element(colorSlotUseCounts_.begin(), colorSlotUseCounts_.end());
        const int slot = static_cast<int>(std::distance(colorSlotUseCounts_.begin(), minIt));
        ++colorSlotUseCounts_[static_cast<size_t>(slot)];
        registry.emplace<PlayerColor>(player,
                                      PlayerColor{
                                          .rgb = player_colors::k_palette[static_cast<size_t>(slot)],
                                          .paletteIdx = slot,
                                      });
    }

    // Assign an animal nickname using the same least-used scheme.  When
    // the future custom-nickname flow lands, it'll set `PlayerName.isCustom`
    // and the auto-assigner here can simply skip that player.  Right now
    // every joiner gets an animal handle.
    {
        const auto minNickIt = std::min_element(nicknameSlotUseCounts_.begin(), nicknameSlotUseCounts_.end());
        const int nickSlot = static_cast<int>(std::distance(nicknameSlotUseCounts_.begin(), minNickIt));
        ++nicknameSlotUseCounts_[static_cast<size_t>(nickSlot)];
        PlayerName pn;
        pn.set(player_nicknames::k_nicknames[static_cast<std::size_t>(nickSlot)]);
        pn.isCustom = false;
        registry.emplace<PlayerName>(player, pn);
    }
    registry.emplace<BeamState>(player);

    const WeaponConfig& rifleConfig = getWeaponConfig(WeaponType::Rifle);
    const WeaponConfig& railConfig = getWeaponConfig(WeaponType::RailGun);
    WeaponState weaponState{};
    weaponState.current = WeaponSlot::PRIMARY;
    getSlot(weaponState, WeaponSlot::PRIMARY) = GunInstance{
        .type = WeaponType::Rifle,
        .totalAmmo = rifleConfig.defaultAmmoCapacity,
        .currentMagAmmo = rifleConfig.magazineSize,
        .fireCooldown = 0.0f,
    };
    getSlot(weaponState, WeaponSlot::SECONDARY) = GunInstance{
        .type = WeaponType::RailGun,
        .totalAmmo = railConfig.defaultAmmoCapacity,
        .currentMagAmmo = railConfig.magazineSize,
        .fireCooldown = 0.0f,
    };
    registry.emplace<WeaponState>(player, weaponState);
    registry.emplace<GrenadeState>(player, makeDefaultGrenadeState());

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
            // Release this player's palette slot so the next joiner can reuse it.
            if (const auto* color = registry.try_get<PlayerColor>(player);
                color != nullptr && color->paletteIdx >= 0 && color->paletteIdx < player_colors::k_paletteSize)
            {
                auto& useCount = colorSlotUseCounts_[static_cast<size_t>(color->paletteIdx)];
                if (useCount > 0) {
                    --useCount;
                }
            }

            // Release the auto-assigned nickname slot too — same scheme.
            // Custom names (PlayerName::isCustom == true) don't reserve a
            // slot, so leave the use-counts alone in that case.
            if (const auto* pn = registry.try_get<PlayerName>(player); pn != nullptr && !pn->isCustom) {
                for (std::size_t i = 0; i < player_nicknames::k_nicknames.size(); ++i) {
                    if (std::strcmp(player_nicknames::k_nicknames[i], pn->c_str()) == 0) {
                        auto& nickUseCount = nicknameSlotUseCounts_[i];
                        if (nickUseCount > 0)
                            --nickUseCount;
                        break;
                    }
                }
            }
            systems::destroyRagdoll(registry, player);
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
    // enemies at `servernow − cl_interp`).  Our client does not do
    // that prediction — it renders at `most_recent_snapshot_apply −
    // cl_interp`.  The most-recent snapshot was generated at
    // `servernow − inbound_RTT/2`, so our client actually renders
    // enemies at `servernow − RTT/2 − cl_interp`.
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
    server->snapshotClientNetStates(netCache);

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

bool ServerGame::isHost(ClientId clientId) const
{
    return lobbyManager.isHost(clientId);
}

bool ServerGame::setKillsToWin(int kills)
{
    return matchController.setKillsToWin(kills);
}

bool ServerGame::setIdleShutdownMinutes(int minutes)
{
    if (minutes <= 0 || minutes > 1440) { // 1440 minutes = 24 hours
        return false;
    }

    idleShutdownEnabled_ = true;
    idleShutdownMs_ = static_cast<uint64_t>(minutes) * 60 * 1000;
    lastNonEmptyMs_ = SDL_GetTicks();

    return true;
}
