/// @file PlayerStatusSystem.hpp
/// @brief Player status manager for things like life state and healing.

#pragma once

#include "ecs/components/Health.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/NetKillEvent.hpp"

/// @brief Player status update system.
namespace systems
{
const float armorMax = 100.0f;
const float healthMax = 100.0f;
const float healCooldown = 5.0f; // In seconds.
const float healingRate = 20.0f; // Healing amount per second.

/// @param amount Healing amount being applied
/// @param playerHealth entity's health component.
void applyHeal(float amount, Health& playerHealth);

/// @param damage  Damage amount being applied.
/// @param player Player who took damage.
/// @param killer player who delt the final blow.
/// @param registry  The ECS registry.
/// @param killEvents  Vector to store kill events.
void applyDamage(
    float damage, entt::entity player, entt::entity& killer, Registry& registry, std::vector<NetKillEvent>& killEvents);

/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runPlayerStatus(Registry& registry, float dt);
} // namespace systems
