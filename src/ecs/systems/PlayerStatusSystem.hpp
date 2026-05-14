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
const float healCooldown = 5.0f; ///< Seconds after last damage before passive healing starts.
const float healingRate = 20.0f; ///< Passive healing amount per second.

/// @brief Apply a healing amount, filling health first then armor.
/// @param amount       Healing amount being applied.
/// @param playerHealth Entity's health component (modified in place).
void applyHeal(float amount, Health& playerHealth);

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
/// @return Final damage value after status modifiers such as powerups. Returns 0 if damage was ignored.
float applyDamage(float damage,
                 entt::entity player,
                 entt::entity& killer,
                 Registry& registry,
                 std::vector<NetKillEvent>& killEvents,
                 BodyRegion hitRegion = BodyRegion::UpperTorso);

/// @brief Run one tick of player status: respawn timers and passive healing.
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runPlayerStatus(Registry& registry, float dt);

/// @brief Tick spawn point cooldowns.  Each spawn point's cooldown decrements
/// by dt and becomes available again when it reaches zero.
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runSpawnPointCooldowns(Registry& registry, float dt);
} // namespace systems
