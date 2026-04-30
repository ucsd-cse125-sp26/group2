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

/// @brief Run one tick of weapon logic for all armed entities.
///
/// For each entity with `[InputSnapshot, Position, CollisionShape, WeaponState]`:
/// handles weapon switching, cooldown ticking, fire/beam/charge processing,
/// hitscan raycasting, projectile spawning, damage application, and ammo refill.
///
/// @param registry    The ECS registry.
/// @param dt          Fixed physics delta time in seconds.
/// @param outParticles Accumulates particle events for network broadcast.
/// @param killEvents  Accumulates kill events for network broadcast.
void runWeapon(Registry& registry,
               float dt,
               std::vector<NetParticleEvent>& outParticles,
               std::vector<NetKillEvent>& killEvents);

} // namespace systems
