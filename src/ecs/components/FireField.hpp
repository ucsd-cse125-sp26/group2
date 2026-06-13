/// @file FireField.hpp
/// @brief Persistent area-of-effect that damages players inside it (Molotov).

#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include "ecs/components/WeaponState.hpp"

/// @brief A burning area on the ground that damages players standing in it.
///
/// Server-side authority: damage applied by FireSystem each tick.
/// Replicated to clients so they can render fire VFX (Task 12).
struct FireField
{
    glm::vec3 position{0.0f};        ///< World-space center.
    float radius = 0.0f;             ///< AoE radius (u).
    float remaining = 0.0f;          ///< Seconds left until field is destroyed.
    float dps = 0.0f;                ///< Damage per second to players inside.
    float tickAccumulator = 0.0f;    ///< Sub-tick accumulator for fixed-rate damage application.
    entt::entity owner = entt::null; ///< Caused-by entity (for kill credit + self-damage scaling).
    WeaponType weaponType = WeaponType::Molotov; ///< Kill-feed icon profile for this field.
};
