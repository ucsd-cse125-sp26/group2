/// @file PlayerStatusSystem.hpp
/// @brief Player status manager for things like life state and healing.

#pragma once

#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/NetKillEvent.hpp"

/// @brief Player status update system.
namespace systems
{
const float armorMax = 100.0f;   ///< Maximum armor value.
const float healthMax = 100.0f;  ///< Maximum health value.
const float overShieldMax = 200.0f;
const float healCooldown = 5.0f; ///< Seconds after last damage before passive healing starts.
const float healingRate = 20.0f; ///< Passive healing amount per second.

/// @brief Apply a healing amount, filling health first then armor.
/// @param amount       Healing amount being applied.
/// @param playerHealth Entity's health component (modified in place).
void applyHeal(float amount, Health& playerHealth);

/// @brief Refresh the bullet-hit movement slow on a player.
///
/// Sets the player's `PlayerSimState::bulletSlowTimer` to
/// `tms::k_bulletHitSlowDuration`, so the next ground-movement tick clamps the
/// wish speed to `k_bulletHitSlowFactor` of normal. Hitscan call sites in
/// WeaponSystem invoke this alongside applyDamage so getting shot punishes the
/// target's mobility; explosion / fire / killzone damage paths skip it.
/// No-op if `player` lacks PlayerSimState (e.g. a hit dummy).
void applyBulletSlow(entt::entity player, Registry& registry);

/// @brief Apply damage to a player, splitting across armor then health.
///
/// Resets the heal cooldown timer.  If health reaches zero, triggers death
/// handling (respawn timer, kill event, stats update).
///
/// @param damage     Damage amount being applied.
/// @param player     Player entity who took damage.
/// @param killer     Entity who dealt the final blow.
/// @param registry   The ECS registry.
/// @param killEvents Accumulates kill events for network broadcast.
/// @param hitRegion  Body region that was hit (for kill feed / headshot tracking).
/// @param shieldMultiplier Effectiveness against shield layers (overShield + armor).
///        1.0 = full; <1.0 makes shields drain slower (energy-vs-energy weapons).
///        Damage spilling into raw health is always applied at full.
/// @return Final damage value after status modifiers such as powerups. Returns 0 if damage was ignored.
float applyDamage(float damage,
                  entt::entity player,
                  entt::entity& killer,
                  Registry& registry,
                  std::vector<NetKillEvent>& killEvents,
                  BodyRegion hitRegion = BodyRegion::UpperTorso,
                  float shieldMultiplier = 1.0f);

/// @brief Run one tick of player status: respawn timers and passive healing.
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runPlayerStatus(Registry& registry, float dt);

/// @brief Tick spawn point cooldowns.  Each spawn point's cooldown decrements
/// by dt and becomes available again when it reaches zero.
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runSpawnPointCooldowns(Registry& registry, float dt);

/// @brief A resolved spawn: safe center position plus the point's facing yaw.
struct SpawnResolution
{
    glm::vec3 center{0.0f};
    float yaw = 0.0f; ///< Authored facing direction (radians) for the chosen spawn point.
};

/// @brief Pick a respawn point and resolve it to a safe spawn center.
///
/// Uses the same cooldown-aware spawn selection and depenetration logic as
/// on-death respawn, so initial join spawns share the live respawn behavior.
/// Spawn points are biased away from living enemies, and the chosen point's
/// authored facing yaw is returned so the caller can orient the player.
/// The player's `CollisionShape` should already be attached so the capsule
/// recovery sweep matches the player's actual shape.
///
/// @param registry  The ECS registry.
/// @param player    The player entity whose spawn position to resolve.
/// @return The resolved spawn center and facing yaw.
SpawnResolution chooseAndResolveSpawnPosition(Registry& registry, entt::entity player);
} // namespace systems
