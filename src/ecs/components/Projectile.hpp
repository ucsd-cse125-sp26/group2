#pragma once

#include <cstdint>
#include <entt/entt.hpp>

/// @brief Weapon type — determines tracer style, damage, sound, and impact effects.
enum class WeaponType : uint8_t
{
    Rifle,       ///< Fast hitscan/projectile (R301-style capsule tracer)
    Shotgun,     ///< Fast spread projectile burst
    Rocket,      ///< Slow arcing projectile (ribbon trail)
    EnergyRifle, ///< Hitscan energy weapon (beam + lightning arcs)
    EnergySMG,   ///< Fast hitscan energy burst
};

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
    WeaponType type = WeaponType::Rifle;
    float damage = 15.f;
    entt::entity owner = entt::null; ///< Entity that fired this projectile.
};
