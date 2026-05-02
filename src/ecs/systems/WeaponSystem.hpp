/// @file WeaponSystem.hpp
/// @brief Weapon state manager system.

#pragma once

#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/NetKillEvent.hpp"
#include "network/ShotDebugReport.hpp" // ShotDebugCapture (PR-20).
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
/// @param outShotDebug PR-20 (server-side only): if non-null, every
///                    hitscan shot pushes a ShotDebugCapture row
///                    while the rewind guard is still active so the
///                    capsule data reflects the historical sample
///                    the server actually raycast against.  ServerGame
///                    serialises and sends each entry to the shooter
///                    via `Server::enqueueTo`.  Client TUs leave this
///                    null — clients run WeaponSystem for prediction
///                    only and don't generate debug reports.
void runWeapon(Registry& registry,
               float dt,
               std::vector<NetParticleEvent>& outParticles,
               std::vector<NetKillEvent>& killEvents,
               std::vector<net::shotdebug::ShotDebugCapture>* outShotDebug = nullptr);

} // namespace systems
