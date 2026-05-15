/// @file Projectile.hpp
/// @brief Projectile component and weapon/surface type enumerations.

#pragma once

#include "WeaponState.hpp"
#include "ecs/physics/SurfaceType.hpp" // SurfaceType moved here in Phase 3; this re-include preserves existing call sites.

#include <cstdint>
#include <entt/entt.hpp>
#include <glm/vec3.hpp>

/// @brief Component attached to projectile entities.
///
/// Velocity comes from the entity's Velocity component.
/// World position comes from the entity's Position component.
struct Projectile
{
    WeaponType type = WeaponType::Rifle; ///< Weapon that spawned this projectile.
    float damage = 0.0f;                 ///< Damage dealt on hit.
    entt::entity owner = entt::null;     ///< Entity that fired this projectile.
    bool explosive = false;              ///< True if the projectile detonates on impact.
    float currentLifeTime = 0.0f;        ///< Seconds since spawn (destroyed when exceeding max).

    // Grenade-specific extensions. Defaults preserve rocket-style behavior.
    float fuseTimer = -1.0f;             ///< Countdown; <0 means no fuse (impact-only).
    float bounceRestitution = 0.0f;      ///< 0 = no bounce. CollisionSystem reflects velocity * this on hit.
    bool sticky = false;                 ///< If true, sets vel=0 on first surface hit and starts fuse.
    glm::vec3 tint = {1.0f, 1.0f, 1.0f}; ///< Render tint multiplier (cosmetic, client-side).
};
