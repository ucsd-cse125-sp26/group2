/// @file WeaponSpawnerSystem.hpp
/// @brief Weapon spawner manager system.

#pragma once
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/registry/Registry.hpp"
#include "entt/entity/entity.hpp"

/// @brief Weapon spawner update system.
namespace systems
{

constexpr float weaponCooldownTime = 10.0f;

/// @param registry    The ECS registry.
/// @param dt          Fixed physics delta time in seconds.
void runWeaponSpawners(Registry& registry, float dt);

} // namespace systems