/// @file ParticleEvents.hpp
/// @brief Event structs dispatched via entt::dispatcher for particle spawning.

#pragma once

#include "ecs/components/Projectile.hpp"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

/// @brief Emitted when a weapon fires (both hitscan and projectile).
struct WeaponFiredEvent
{
    entt::entity shooter = entt::null;
    WeaponType type = WeaponType::Rifle;
    glm::vec3 origin{};    ///< Muzzle world position.
    glm::vec3 direction{}; ///< Normalised fire direction.
    bool isHitscan = false;
    glm::vec3 hitPos{};    ///< Valid only when isHitscan == true.
};

/// @brief Emitted when a projectile or hitscan hits a surface.
struct ProjectileImpactEvent
{
    glm::vec3 pos{};
    glm::vec3 normal{};
    SurfaceType surface = SurfaceType::Concrete;
    WeaponType weaponType = WeaponType::Rifle;
};

/// @brief Emitted when a rocket/grenade explodes.
struct ExplosionEvent
{
    glm::vec3 pos{};
    float blastRadius = 100.f;
};
