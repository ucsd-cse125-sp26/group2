/// @file HealthPackSpawnerSystem.hpp
/// @brief Health pack spawner manager system.

#pragma once
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/registry/Registry.hpp"
#include "entt/entity/entity.hpp"

/// @brief Health pack spawner update system.
namespace systems
{

constexpr float healthPackCooldownTime = 15.0f;

/// @brief Tick spawners: check player overlap for pickup, manage cooldowns.
/// @param registry    The ECS registry.
/// @param dt          Fixed physics delta time in seconds.
void runHealthPackSpawners(Registry& registry, float dt);

} // namespace systems