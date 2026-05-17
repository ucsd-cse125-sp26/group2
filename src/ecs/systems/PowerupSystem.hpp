/// @file PowerupSystem.hpp
/// @brief Powerup state manager system.

#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Powerup state update system.
namespace systems
{

/// @brief Run one tick of powerup logic for all players.
///
/// @param registry    The ECS registry.
/// @param dt          Fixed physics delta time in seconds.

void runPowerups(Registry& registry, float dt);

} // namespace systems
