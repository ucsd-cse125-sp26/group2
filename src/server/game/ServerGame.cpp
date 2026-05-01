/// @file ServerGame.cpp
/// @brief Implementation of the server-side game loop, tick logic, and player management.

#include "ServerGame.hpp"

#include "client/animation/CharacterAnimator.hpp"
#include "ecs/AssetCatalog.hpp"
#include "ecs/components/BeamState.hpp"
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
#include "network/ShotEvent.hpp"
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
    {
        const char* base = SDL_GetBasePath();
        const std::string mapPath = std::string(base ? base : "") + "assets/" + kMapAsset.filename;

        physics::MapLoadOptions opts;
        opts.scale = kMapAsset.loadScale;
        opts.allMeshesAreCollision = true; // Prototype map — every mesh is collision.
        opts.addFloorPlane = false;        // Map geometry provides its own floor.

        if (physics::loadMapCollision(mapPath, mapCollision_, opts)) {
            SDL_Log("[server] map collision loaded: %zu planes, %zu boxes, %zu brushes",
                    mapCollision_.planes.size(),
                    mapCollision_.boxes.size(),
                    mapCollision_.brushes.size());
        } else {
            SDL_Log("[server] WARNING: map collision load failed — falling back to testWorld()");
        }

        // Load prop collision — must match client for prediction parity.
        const std::string assetsDir = std::string(base ? base : "") + "assets/";
        for (const AssetDefinition& def : kPropAssets)
            physics::loadPropCollision(assetsDir + def.filename, mapCollision_, def.loadTranslation, def.loadScale);

        // Set active world with map + all props.
        physics::setActiveWorld(mapCollision_.geometry());
    }

    if (!server.init(addr, port, transport))
        return false;

    // ── Load animation subsystem for hitbox detection ──
    initAnimation();

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

    // Static respawn points
    const entt::entity playerSpawner1 = registry.create();
    registry.emplace<RespawnPoint>(playerSpawner1);
    registry.emplace<Position>(playerSpawner1, glm::vec3{0.0f, 2000.0f, 0.0f});

    const entt::entity playerSpawner2 = registry.create();
    registry.emplace<RespawnPoint>(playerSpawner2);
    registry.emplace<Position>(playerSpawner2, glm::vec3{800.0f, 40.0f, 800.0f});

    const entt::entity playerSpawner3 = registry.create();
    registry.emplace<RespawnPoint>(playerSpawner3);
    registry.emplace<Position>(playerSpawner3, glm::vec3{800.0f, 430.0f, -800.0f});

    const entt::entity playerSpawner4 = registry.create();
    registry.emplace<RespawnPoint>(playerSpawner4);
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
    default:
        break;
    }
}

void ServerGame::tick(float dt, Uint64 nextTick)
{
    while (!server.isEmpty()) {
        const Event event = server.dequeueEvent();
        eventHandler(event);

        // Check tick time --> move to next if over
        if (const Uint64 kNow = SDL_GetPerformanceCounter(); kNow >= nextTick) {
            // TODO: Drop events in queue
            SDL_Log("[server] Exceeded tick time for event handling.");
            break;
        }
    }

    // Update server-side animation and hitbox capsules before weapon raycasts.
    updateAnimationAndHitboxes(dt);

    // Phase 6: capture this tick's capsules into each entity's HitboxHistory
    // ring. Has to run *after* updateHitboxes (so the capsules reflect this
    // tick's pose) and *before* runWeapon (so the upcoming hitscans share a
    // consistent history snapshot from which rewindHitboxes can pick).
    systems::pushHitboxHistory(registry, static_cast<uint32_t>(tickCount));

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

    std::vector<NetParticleEvent> particleEvents;
    systems::runWeapon(registry, dt, particleEvents, pendingKillEvents);
    systems::runMovement(registry, dt, physics::activeWorld());
    systems::runCollision(registry, dt, physics::activeWorld());
    systems::runExplosion(registry, particleEvents, pendingKillEvents);
    systems::runPlayerStatus(registry, dt);
    systems::runWeaponSpawners(registry, dt);

    matchController.update(dt, registry, server);

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
        server.broadcastRegistry(registry);
    }
    server.broadcastParticleEvents(particleEvents);
    server.broadcastKillEvents(pendingKillEvents);
    pendingKillEvents.clear();

    ++tickCount;

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
    // Server-side cap on rewind depth. 25 ticks at 128 Hz ≈ 195 ms,
    // matching the plan's "server-capped at 200 ms" rule. Above this
    // a high-ping shooter could rewind targets behind cover that the
    // victim has long since ducked behind, producing the classic
    // "I shot him around the corner" feel for the receiver.
    static constexpr uint32_t k_maxLagCompTicks = 25;

    const auto currentServerTick = static_cast<uint32_t>(tickCount);
    for (const auto& [clientId, entity] : clientEntities) {
        if (!registry.valid(entity))
            continue;

        const uint16_t rttMs = server.getClientRttMs(clientId);
        // Half-RTT in ticks. Round-to-nearest by adding half a tick
        // before integer divide. (rttMs / 2 / 1000 * tickRateHz)
        // reordered to keep the integer-only arithmetic exact at the
        // cost of one extra add: rttHalfTicks = (rttMs * Hz + 1000) /
        // 2000.
        const uint32_t rttHalfTicks =
            (static_cast<uint32_t>(rttMs) * static_cast<uint32_t>(tickRateHz) + 1000u) / 2000u;
        const uint32_t lagTicks = std::min<uint32_t>(rttHalfTicks, k_maxLagCompTicks);
        const uint32_t targetTick = (lagTicks == 0 || lagTicks >= currentServerTick)
                                        ? 0u // explicit "no rewind" sentinel
                                        : (currentServerTick - lagTicks);

        registry.emplace_or_replace<LagCompTarget>(entity, LagCompTarget{.targetServerTick = targetTick});

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

    // Step 1: Update each server-side animator with current entity state.
    for (auto& [entity, animator] : serverAnimators_) {
        if (!registry.valid(entity))
            continue;

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

        // Write model-space joint matrices into the ECS component.
        const auto& jointMats = animator->jointModelMatrices();
        auto& jm = registry.get_or_emplace<JointMatrices>(entity);
        jm.matrices = jointMats;
    }

    // Step 2: Transform bone poses into world-space hitbox capsules.
    systems::updateHitboxes(registry, hitboxRig_, rigScale_, rigMeshMinY_);
}
