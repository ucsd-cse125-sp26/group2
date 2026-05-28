/// @file Killzone.hpp
/// @brief Killzone trigger information (lava pits, void floors, etc.).

#pragma once

/// @brief ECS component: trigger volume that kills any player whose AABB
/// overlaps it. Authored in Blender with custom property `entity_type = 4`
/// on an object covering the lethal region; the map loader fills position
/// from the Blender transform and `ServerGame` instantiates the entity.
///
/// The damage amount is intentionally far above the maximum effective
/// health (armor + overshield) so the player always dies on contact.
struct Killzone
{
    /// @brief Damage applied per overlap tick. Default is lethal in one tick.
    float damage = 100000.0f;
};
