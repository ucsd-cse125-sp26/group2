/// @file PlayerStatusSystem.cpp
/// @brief Player status manager system.

#include "PlayerStatusSystem.hpp"

#include "AbilitySystem.hpp"
#include "PowerupSpawnerSystem.hpp"
#include "SDL3/SDL_log.h"
#include "ecs/components/AbilityState.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/DeathInfo.hpp"
#include "ecs/components/DroppedWeapon.hpp"
#include "ecs/components/GrenadeState.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerMatchStats.hpp"
#include "ecs/components/PlayerSimState.hpp" // also pulls in PlayerVisState
#include "ecs/components/Position.hpp"
#include "ecs/components/PowerupState.hpp"
#include "ecs/components/Renderable.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/DroppedWeaponSystem.hpp"
#include "ecs/systems/RagdollSystem.hpp"
#include "network/NetKillEvent.hpp"

#include <ecs/components/RespawnPoint.hpp>
#include <algorithm>
#include <limits>
#include <random>
#include <vector>

namespace systems
{
/// @copydoc applyHeal
void applyHeal(float amount, Health& playerHealth)
{
    if (amount < 0)
        return;

    if (playerHealth.health < systems::healthMax) {
        if ((playerHealth.health + amount) > systems::healthMax) {
            amount -= systems::healthMax - playerHealth.health;
            playerHealth.health = systems::healthMax;
            playerHealth.armor = amount;
        } else {
            playerHealth.health += amount;
        }

    } else if ((playerHealth.armor + amount) <= systems::armorMax) {
        playerHealth.armor += amount;
    } else {
        playerHealth.armor = systems::armorMax;
    }
}

/// @brief Cooldown duration set on a spawn point after a player spawns there.
inline constexpr float k_spawnPointCooldown = 5.0f;
inline constexpr float k_spawnPushback = 0.03125f;

inline float standingHalfHeight(const CollisionShape& shape)
{
    if (shape.type == CollisionShapeType::Capsule) {
        return shape.radius + shape.halfHeight;
    }

    return shape.halfExtents.y;
}

inline physics::CapsuleShape spawnCapsuleFor(const CollisionShape& shape)
{
    return physics::CapsuleShape{
        .radius = shape.type == CollisionShapeType::Capsule ? shape.radius
                                                            : std::max(shape.halfExtents.x, shape.halfExtents.z),
        .halfHeight = shape.type == CollisionShapeType::Capsule
                          ? shape.halfHeight
                          : std::max(0.0f, shape.halfExtents.y - shape.halfExtents.x),
        .up = glm::vec3{0.0f, 1.0f, 0.0f},
    };
}

inline glm::vec3 resolveRespawnPosition(Registry& registry, entt::entity player, glm::vec3 feetPosition)
{
    CollisionShape shape{};
    if (const auto* existingShape = registry.try_get<CollisionShape>(player)) {
        shape = *existingShape;
    }

    glm::vec3 resolved = feetPosition + glm::vec3{0.0f, standingHalfHeight(shape), 0.0f};
    glm::vec3 zeroVelocity{0.0f};
    const physics::CapsuleShape capsule = spawnCapsuleFor(shape);
    const physics::WorldGeometry& world = physics::activeWorld();

    const float maxSpawnRecovery = std::max(256.0f, standingHalfHeight(shape) * 4.0f);
    const physics::DepenetrationResult depen = physics::depenetrateCapsuleVsWorldDetailed(
        resolved,
        zeroVelocity,
        capsule,
        world,
        physics::DepenetrationOptions{.maxPushDistance = maxSpawnRecovery, .allowEmergencyUnstick = true});

    if (depen.unresolvedOverlap) {
        SDL_Log("[respawn] spawn point at %.1f %.1f %.1f remained embedded after recovery",
                static_cast<double>(feetPosition.x),
                static_cast<double>(feetPosition.y),
                static_cast<double>(feetPosition.z));
    }

    const physics::GroundProbeResult ground = physics::probeGround(capsule, resolved, k_spawnPushback * 2.0f, world);
    if (ground.hit && ground.walkable) {
        const float targetAlongUp =
            ground.point.y + ground.normal.y * (k_spawnPushback + capsule.minkowskiExtent(ground.normal));
        resolved.y += targetAlongUp - resolved.y;
    }

    return resolved;
}

/// @brief Choose a respawn point with cooldown-aware selection.
///
/// Prefers available (cooldown = 0) spawn points, picking randomly among them.
/// If all spawn points are on cooldown, picks the one with the lowest remaining
/// cooldown.  Sets a 3-second cooldown on the chosen point.
inline glm::vec3 chooseRespawnPoint(Registry& registry)
{
    auto view = registry.view<RespawnPoint, Position>();

    // Collect available (off-cooldown) spawn points.
    std::vector<entt::entity> available;
    entt::entity lowestCooldownEntity = entt::null;
    float lowestCooldown = std::numeric_limits<float>::max();

    for (entt::entity e : view) {
        auto& sp = view.get<RespawnPoint>(e);
        if (sp.available) {
            available.push_back(e);
        }
        if (sp.cooldown < lowestCooldown) {
            lowestCooldown = sp.cooldown;
            lowestCooldownEntity = e;
        }
    }

    entt::entity chosen = entt::null;

    if (!available.empty()) {
        // Pick randomly among available spawn points.
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<std::size_t> dist(0, available.size() - 1);
        chosen = available[dist(gen)];
    } else if (lowestCooldownEntity != entt::null) {
        // All on cooldown — pick the one closest to being ready.
        chosen = lowestCooldownEntity;
    }

    if (chosen != entt::null) {
        // Set cooldown on the chosen spawn point.
        auto& sp = view.get<RespawnPoint>(chosen);
        sp.cooldown = k_spawnPointCooldown;
        sp.available = false;
        return view.get<Position>(chosen).value;
    }

    // Fallback when no spawn points exist in the world.
    return glm::vec3(0.0f, 200.0f, 0.0f);
}

/// @brief Reset a dead player to a fresh spawn state.
///
/// Clears the respawn timer and death info, restores visibility, resets
/// position/velocity/health/weapons to defaults, and places the player
/// at the spawn point.
///
/// @param player    Entity to respawn (modified in place).
/// @param registry  The ECS registry.
inline void handleRespawn(entt::entity& player, Registry& registry)
{
    const WeaponConfig& rifleConfig = getWeaponConfig(WeaponType::Rifle);
    const WeaponConfig& railConfig = getWeaponConfig(WeaponType::RailGun);
    const glm::vec3 respawnFeet = chooseRespawnPoint(registry);
    const glm::vec3 respawnCenter = resolveRespawnPosition(registry, player, respawnFeet);

    registry.erase<RespawnTimer>(player);
    registry.erase<DeathInfo>(player);
    registry.patch<Renderable>(player, [](Renderable& rend) { rend.visible = true; });
    registry.emplace_or_replace<InputSnapshot>(player);
    registry.emplace_or_replace<Position>(player, respawnCenter);
    registry.emplace_or_replace<Velocity>(player);
    registry.emplace_or_replace<PlayerVisState>(player);
    registry.emplace_or_replace<PlayerSimState>(player);
    registry.emplace_or_replace<Health>(player, Health{});
    if (auto* abilityState = registry.try_get<AbilityState>(player)) {
        abilityState->primaryCooldown = 0.0f;
        abilityState->primaryActive = false;
        abilityState->secondaryCooldown = 0.0f;
        abilityState->secondaryActive = false;
        abilityState->recallMarkerSet = false;
    }
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
    registry.emplace_or_replace<WeaponState>(player, weaponState);
    registry.emplace_or_replace<GrenadeState>(player, makeDefaultGrenadeState());
}

/// @brief Transition a player to the dead state if health has reached zero.
///
/// Hides the player, removes hitboxes, starts a 5-second respawn timer,
/// updates death/kill stats, and emits a NetKillEvent for the kill feed.
///
/// @param player       Entity that died.
/// @param playerHealth Health component (already at or below zero).
/// @param killer       Entity that dealt the killing blow.
/// @param registry     The ECS registry.
/// @param killEvents   Accumulates kill events for network broadcast.
/// @param hitRegion    Body region of the killing blow.
inline void handleDeath(entt::entity& player,
                        Health& playerHealth,
                        entt::entity& killer,
                        Registry& registry,
                        std::vector<NetKillEvent>& killEvents,
                        BodyRegion hitRegion)
{
    if (playerHealth.health <= 0) {
        // Drop the player's two weapons at their current position.
        // Both slots always carry a GunInstance, so both always drop —
        // including the default Rifle/RailGun loadout. Pickup preserves
        // the at-death ammo state.  The two drops are offset along the
        // player's facing-right axis so the picker can comfortably aim
        // at one without grabbing both.
        const Position deathPos = registry.get<Position>(player);
        const WeaponState& deathWeapons = registry.get<WeaponState>(player);
        const float yawAtDeath = registry.get<InputSnapshot>(player).yaw;
        const glm::vec3 rightAxis{std::cos(yawAtDeath), 0.0f, -std::sin(yawAtDeath)};
        constexpr float k_dropSideOffset = 32.0f; // ~AABB width — clear gap between the two drops
        auto spawnDrop = [&](const GunInstance& g, float side) {
            const entt::entity e = registry.create();
            registry.emplace<Position>(e, deathPos.value + rightAxis * (side * k_dropSideOffset));
            registry.emplace<CollisionShape>(e);
            registry.emplace<DroppedWeapon>(e,
                                            DroppedWeapon{
                                                .type = g.type,
                                                .totalAmmo = g.totalAmmo,
                                                .currentMagAmmo = g.currentMagAmmo,
                                                .despawnTimer = systems::k_droppedWeaponLifetime,
                                            });
        };
        spawnDrop(getSlot(deathWeapons, WeaponSlot::PRIMARY), -1.0f);
        spawnDrop(getSlot(deathWeapons, WeaponSlot::SECONDARY), +1.0f);

        // Phase 13 ragdoll: capture pre-death velocity BEFORE we clear it
        // below, so the corpse inherits the player's motion at the moment
        // of death (rocket-juggled corpses keep their fling momentum).
        // The renderer hides the kinematic player (Renderable visible=false)
        // and instead reads the 15 ragdoll bone transforms via
        // `RagdollBoneTag` to drive the skinned-mesh palette.
        spawnRagdoll(registry, player);

        // Update death
        registry.get_or_emplace<PlayerVisState>(player).isDead = true;
        registry.get_or_emplace<Velocity>(player) = Velocity{};
        registry.patch<Renderable>(player, [](Renderable& rend) { rend.visible = false; });
        registry.remove<HitboxInstance>(player);
        registry.emplace_or_replace<RespawnTimer>(player, RespawnTimer{.timeRemaining = 5.0f});
        registry.patch<PlayerMatchStats>(player, [&](PlayerMatchStats& stats) { stats.deaths++; });

        // Clear input so dead players don't continue shooting/moving.
        registry.emplace_or_replace<InputSnapshot>(player);

        // Award killer
        if (killer != player) {
            registry.get_or_emplace<PlayerMatchStats>(killer).kills++;
        }

        // Get killer info
        ClientId killerId = registry.get<ClientId>(killer);
        Health killerHealth = registry.get<Health>(killer);

        // Death info handling
        NetKillEvent event{
            .killerId = killerId,
            .victimId = registry.get<ClientId>(player),
            .killerHealth = killerHealth,
            .hitRegion = hitRegion,
            .isHeadshot = (hitRegion == BodyRegion::Head),
        };
        killEvents.push_back(event);

        registry.emplace_or_replace<DeathInfo>(player,
                                               DeathInfo{
                                                   .killerId = killerId,
                                                   .killerHealth = killerHealth,
                                               });
    }
}

inline void updateAbilityLevel(Registry& registry, entt::entity player, float dmg)
{
    if (dmg < 0)
        return;

    AbilityState& abilityState = registry.get<AbilityState>(player);
    systems::grantAbilityProgress(abilityState, dmg);
}

inline float absorbDamage(float& pool, float damage)
{
    if (pool < 0) {
        pool = 0;
        return damage;
    }

    const float absorbed = std::min(pool, damage);
    pool -= absorbed;
    return damage - absorbed;
}

float applyDamage(float damage,
                  entt::entity player,
                  entt::entity& killer,
                  Registry& registry,
                  std::vector<NetKillEvent>& killEvents,
                  BodyRegion hitRegion)
{
    // If player is dead, ignore damage
    if (registry.all_of<RespawnTimer>(player))
        return 0.0f;

    Health& playerHealth = registry.get_or_emplace<Health>(player);

    // Reset heal cooldown on every damage tick
    playerHealth.healTimer = systems::healCooldown;

    if (player != killer && registry.valid(killer)) {
        const PowerupState* powerupState = registry.try_get<PowerupState>(killer);
        const PowerupConfig damageConfig = getPowerupConfig(PowerupType::Damage);
        if (powerupState != nullptr && hasPowerup(*powerupState, PowerupType::Damage)) {
            damage = damage * damageConfig.amount;
        }

        updateAbilityLevel(registry, killer, damage);
    }

    float remainingDamage = damage;

    remainingDamage = absorbDamage(playerHealth.overShield, remainingDamage);
    remainingDamage = absorbDamage(playerHealth.armor, remainingDamage);

    if (remainingDamage > 0.0f) {
        if (playerHealth.health <= remainingDamage) {
            playerHealth.health = 0.0f;
            handleDeath(player, playerHealth, killer, registry, killEvents, hitRegion);
        } else {
            playerHealth.health -= remainingDamage;
        }
    }

    return damage;
}

/// @brief Tick passive health regeneration after the heal cooldown expires.
/// @param playerHealth  Health component (modified in place).
/// @param dt            Fixed physics delta time in seconds.
inline void handleHealing(Health& playerHealth, float dt)
{
    if (playerHealth.healTimer == 0) {
        const float healingAmount = healingRate * dt;
        applyHeal(healingAmount, playerHealth);
    } else {
        playerHealth.healTimer -= dt;
        if (playerHealth.healTimer < 0)
            playerHealth.healTimer = 0;
    }
}

void runPlayerStatus(Registry& registry, float dt)
{
    registry.view<Player, InputSnapshot>().each([&registry, dt](entt::entity e, InputSnapshot& snap) {
        if (registry.all_of<RespawnTimer>(e)) {
            auto& respawnTimer = registry.get<RespawnTimer>(e);

            // Allow skipping the respawn timer early by pressing space.
            if (snap.skipRespawn) {
                snap.skipRespawn = false;
                respawnTimer.timeRemaining = 0.0f;
            }

            respawnTimer.timeRemaining -= dt;
            if (respawnTimer.timeRemaining <= 0) {
                handleRespawn(e, registry);
            }

            // Consume killSelf while dead — prevent re-death on respawn.
            snap.killSelf = false;
        } else {
            // Clear stale skipRespawn from previous death cycle so it
            // doesn't fire instantly if the player dies again.
            snap.skipRespawn = false;

            Health& playerHealth = registry.get_or_emplace<Health>(e);
            handleHealing(playerHealth, dt);

            // Only process killSelf when alive.
            if (snap.killSelf) {
                snap.killSelf = false;
                std::vector<NetKillEvent> kills;
                applyDamage(999.0f, e, e, registry, kills, BodyRegion::Head);
            }
        }
    });
}

void runSpawnPointCooldowns(Registry& registry, float dt)
{
    registry.view<RespawnPoint>().each([dt](RespawnPoint& sp) {
        if (!sp.available) {
            sp.cooldown -= dt;
            if (sp.cooldown <= 0.0f) {
                sp.cooldown = 0.0f;
                sp.available = true;
            }
        }
    });
}
} // namespace systems
