/// @file WeaponState.hpp
/// @brief Weapon state component for armed entities.

#pragma once

#include <cstdint>

/// @brief Weapon type — determines tracer style, damage, sound, and impact effects.
enum class WeaponType : uint8_t
{
    Rifle,     ///< Fast hitscan/projectile (R301-style capsule tracer)
    Rocket,    ///< Slow arcing projectile (ribbon trail)
    RailGun,   ///< Hitscan energy weapon (beam + lightning arcs)
    EnergyGun, ///< Fast hitscan energy burst
    HEGrenade, ///< Bouncy grenade with 3s fuse, lethal explosion
    Molotov,   ///< Impact-detonate, leaves a fire field (damage over time)
    Impulse,   ///< Sticky 1s fuse, big knockback, no damage (movement tool)
};

enum class WeaponSlot : uint8_t
{
    PRIMARY,
    SECONDARY,
};

/// @brief Struct that defines this weapon's type, cooldown, and ammo.
struct GunInstance
{
    WeaponType type = WeaponType::Rifle;
    int totalAmmo = 0;
    int currentMagAmmo = 0;
    float fireCooldown = 0.f;
    float chargeTime = 0.f; ///< Accumulated charge time (charge weapons only).
};

/// @brief Component attached to armed entities (players).
struct WeaponState
{
    GunInstance primary;
    GunInstance secondary;
    WeaponSlot current = WeaponSlot::PRIMARY; ///< Currently equipped weapon slot.
};
