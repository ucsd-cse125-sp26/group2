/// @file DroppedWeaponSystem.hpp
/// @brief Update system for player-dropped weapon pickups.

#pragma once

#include "ecs/registry/Registry.hpp"

namespace systems
{

/// @brief Default lifetime (seconds) for a dropped weapon before it despawns.
constexpr float k_droppedWeaponLifetime = 30.0f;

/// @brief Tick dropped weapons: handle pickup (overlap-refill or look+F replace) and despawn timer.
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runDroppedWeapons(Registry& registry, float dt);

} // namespace systems
