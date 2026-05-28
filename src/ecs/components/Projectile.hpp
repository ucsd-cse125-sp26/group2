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

    // Stick state (sticky grenades). Once `stuck`, the projectile is frozen in
    // place and its fuse runs to detonation — no gravity, no further collision.
    // If `stuckTo` is a live player, the projectile also rides that player
    // (pos = host pos + stuckOffset) and detonation guarantees a kill on the
    // host. A null `stuckTo` while `stuck` means it is glued to static world
    // geometry (wall / ceiling / floor).
    bool stuck = false;                  ///< True once attached to a surface or player.
    entt::entity stuckTo = entt::null;   ///< Player this grenade is stuck to, or null (world).
    glm::vec3 stuckOffset{0.0f};         ///< Offset from the host's origin at stick time.

    glm::vec3 tint = {1.0f, 1.0f, 1.0f}; ///< Render tint multiplier (cosmetic, client-side).
};
