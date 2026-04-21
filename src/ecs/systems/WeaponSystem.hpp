/// @file WeaponSystem.hpp
/// @brief Weapon state manager system.

#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Weapon state update system.
namespace systems
{

/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runWeapon(Registry& registry, float dt);

} // namespace systems
