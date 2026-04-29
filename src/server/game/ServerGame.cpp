/// @file ServerGame.cpp
/// @brief Implementation of the server-side game loop, tick logic, and player management.

#include "ServerGame.hpp"

#include "ecs/components/BeamState.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Renderable.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/ExplosionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "ecs/systems/WeaponSystem.hpp"
#include "network/ShotEvent.hpp"

#include <SDL3/SDL.h>

bool ServerGame::init(const char* addr, Uint16 port, int hz)
{
    tickRateHz = hz;
    clientEntities.clear(); // For safety
    registry.clear();

    // ── Load map collision ──────────────────────────────────────────────
    // Scale: the map was authored in meters; the game uses Quake units (inches).
    {
        static constexpr float k_metersToInches = 39.3701f;

        const char* base = SDL_GetBasePath();
        const std::string mapPath = std::string(base ? base : "") + "assets/maps/map1.glb";

        physics::MapLoadOptions opts;
        opts.scale = k_metersToInches;
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
        physics::loadPropCollision(
            assetsDir + "free_1975_porsche_911_930_turbo.glb", mapCollision_, glm::vec3(-200.0f, 1.3f, 400.0f), 40.0f);
        physics::loadPropCollision(
            assetsDir + "metallic_pallet_factory_store.glb", mapCollision_, glm::vec3(0.0f, 0.0f, 600.0f), 0.25f);
        physics::loadPropCollision(assetsDir + "bottle_a.glb", mapCollision_, glm::vec3(100.0f, 0.0f, 400.0f), 20.0f);

        // Set active world with map + all props.
        physics::setActiveWorld(mapCollision_.geometry());
    }

    if (!server.init(addr, port))
        return false;

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
    const entt::entity spawner = registry.create();
    registry.emplace<WeaponSpawner>(spawner,
                                    WeaponSpawner{.type = WeaponType::Rifle, .spawnCooldown = 0, .hasWeapon = false});
    registry.emplace<Position>(spawner, glm::vec3{12.0f, 1.0f, 12.0f});
    registry.emplace<CollisionShape>(spawner);

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

    std::vector<NetParticleEvent> particleEvents;
    systems::runWeapon(registry, dt, particleEvents, pendingKillEvents);
    systems::runMovement(registry, dt, physics::activeWorld());
    systems::runCollision(registry, dt, physics::activeWorld());
    systems::runExplosion(registry, particleEvents, pendingKillEvents);
    systems::runPlayerStatus(registry, dt);

    matchController.update(dt, registry, server);

    // Update Client by sending the registry
    server.broadcastRegistry(registry);
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
    registry.emplace<PlayerState>(player);
    registry.emplace<Renderable>(player, Renderable{.modelIndex = 1, .scale = glm::vec3(100.0f)});
    registry.emplace<Health>(player, Health{}); // Defaults to 100/100 health and 100/100 armor
    registry.emplace<PlayerMatchStats>(player, PlayerMatchStats{});
    registry.emplace<BeamState>(player);

    const WeaponConfig& rifleConfig = getWeaponConfig(WeaponType::Rifle);
    const WeaponConfig& railConfig = getWeaponConfig(WeaponType::RailGun);
    const WeaponConfig& wingmanConfig = getWeaponConfig(WeaponType::EnergyGun);
    const WeaponConfig& rocketConfig = getWeaponConfig(WeaponType::Rocket);
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
                                      .tertiary =
                                          GunInstance{
                                              .type = WeaponType::EnergyGun,
                                              .totalAmmo = wingmanConfig.defaultAmmoCapacity,
                                              .currentMagAmmo = wingmanConfig.magazineSize,
                                              .fireCooldown = 0.0f,
                                          },
                                      .quaternary =
                                          GunInstance{
                                              .type = WeaponType::Rocket,
                                              .totalAmmo = rocketConfig.defaultAmmoCapacity,
                                              .currentMagAmmo = rocketConfig.magazineSize,
                                              .fireCooldown = 0.0f,
                                          },
                                      .current = WeaponSlot::PRIMARY,
                                  });

    SDL_Log("[server] spawned player entity for client %d", clientId.value);
}

void ServerGame::deletePlayerEntity(ClientId clientId)
{
    if (const auto it = clientEntities.find(clientId); it != clientEntities.end()) {
        const entt::entity player = it->second;
        if (registry.valid(player)) {
            registry.destroy(player);
        }
        clientEntities.erase(it);
    }
}
