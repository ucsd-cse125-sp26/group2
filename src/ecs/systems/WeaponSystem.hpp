/// @file WeaponSystem.hpp
/// @brief Weapon state manager system.

#pragma once

#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/NetKillEvent.hpp"
#include "network/ShotEvent.hpp"

#include <vector>

/// @brief Weapon state update system.
namespace systems
{

/// @param registry    The ECS registry.
/// @param dt          Fixed physics delta time in seconds.
/// @param outParticles Accumulates particle events for network broadcast.
/// @param killEvents  Accumulates kill events for network broadcast.
void runWeapon(Registry& registry,
               float dt,
               std::vector<NetParticleEvent>& outParticles,
               std::vector<NetKillEvent>& killEvents);

} // namespace systems
