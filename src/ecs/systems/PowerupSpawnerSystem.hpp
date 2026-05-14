/// @file PowerupSpawnerSystem.hpp
/// @brief Powerup spawner manager system.

#pragma once
#include "ecs/components/PowerupSpawner.hpp"
#include "ecs/components/PowerupState.hpp"
#include "ecs/registry/Registry.hpp"

/// @brief Powerup spawner update system.
namespace systems
{

bool hasPowerup(const PowerupState& state, PowerupType type);

/// @brief Tick Powerup spawners: check player overlap for pickup, manage cooldowns.
/// @param registry    The ECS registry.
/// @param dt          Fixed physics delta time in seconds.
void runPowerupSpawners(Registry& registry, float dt);

} // namespace systems
