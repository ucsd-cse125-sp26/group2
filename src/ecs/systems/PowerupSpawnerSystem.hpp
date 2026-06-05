/// @file PowerupSpawnerSystem.hpp
/// @brief Powerup spawner manager system.

#pragma once
#include "ecs/components/PowerupSpawner.hpp"
#include "ecs/components/PowerupState.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/MatchConfig.hpp"

/// @brief Powerup spawner update system.
namespace systems
{

bool hasPowerup(const PowerupState& state, PowerupType type);
void removePowerup(PowerupState& state, PowerupType type);

/// @brief Clamp pending powerup cooldowns to the current host-managed timing settings.
void applyPowerupSpawnerConfig(Registry& registry, const MatchConfig& matchConfig);

/// @brief Hide all powerups and restart their initial match-start spawn delay.
void resetPowerupSpawnersForMatch(Registry& registry, const MatchConfig& matchConfig);

/// @brief Tick Powerup spawners: check player overlap for pickup, manage cooldowns.
/// @param registry    The ECS registry.
/// @param dt          Fixed physics delta time in seconds.
/// @param matchConfig Current host-managed match settings.
void runPowerupSpawners(Registry& registry, float dt, const MatchConfig& matchConfig);

} // namespace systems
