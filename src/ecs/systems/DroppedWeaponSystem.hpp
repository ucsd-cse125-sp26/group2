/// @file DroppedWeaponSystem.hpp
/// @brief Update system for player-dropped weapon pickups.

#pragma once

#include "ecs/components/WeaponState.hpp"
#include "ecs/registry/Registry.hpp"

#include <glm/vec3.hpp>

namespace systems
{

/// @brief Default lifetime (seconds) for a dropped weapon before it despawns.
constexpr float k_droppedWeaponLifetime = 30.0f;

/// @brief Pickup-immunity applied to a weapon dropped during a swap, so the
///        player can't instantly re-grab the gun they just replaced.
constexpr float k_swapDropPickupDelay = 0.8f;

/// @brief A weapon drop queued during view iteration, spawned afterward.
///
/// Creating the drop entity mid-iteration would invalidate the active view, so
/// pickup handlers record the swap-out here and the system spawns them after.
struct PendingWeaponDrop
{
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    GunInstance gun{};
    float pickupDelay = 0.0f;
};

/// @brief Spawn an in-world dropped-weapon pickup carrying a gun's ammo.
///
/// Shared by death drops (PlayerStatusSystem) and weapon-swap drops
/// (WeaponSpawnerSystem / DroppedWeaponSystem). The entity gets a compact
/// AABB plus Velocity/RigidBody so it falls under gravity and settles.
/// @param registry     The ECS registry.
/// @param pos          World position to spawn the drop at.
/// @param initialVel   Initial velocity (e.g. a gentle toss for swap drops).
/// @param gun          The gun being dropped; its ammo state is snapshotted.
/// @param pickupDelay  Pickup-immunity window (s) before the drop can be grabbed.
void spawnDroppedWeapon(
    Registry& registry, glm::vec3 pos, glm::vec3 initialVel, const GunInstance& gun, float pickupDelay);

/// @brief Tick dropped weapons: handle pickup (overlap-refill or look+F replace) and despawn timer.
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runDroppedWeapons(Registry& registry, float dt);

} // namespace systems
