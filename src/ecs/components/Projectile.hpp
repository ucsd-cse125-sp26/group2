/// @file Projectile.hpp
/// @brief Projectile component and weapon/surface type enumerations.

#pragma once

#include "WeaponState.hpp"

#include <cstdint>
#include <entt/entt.hpp>

/// @brief Surface material hit by a projectile — drives impact effect parameters.
enum class SurfaceType : uint8_t
{
    Metal,
    Concrete,
    Flesh,
    Wood,
    Energy,
};

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
};
