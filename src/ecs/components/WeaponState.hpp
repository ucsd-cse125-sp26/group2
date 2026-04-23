/// @file WeaponState.hpp
/// @brief Weapon state component for armed entities.

#pragma once


/// @brief Weapon type — determines tracer style, damage, sound, and impact effects.
enum class WeaponType : uint8_t
{
    Rifle,     ///< Fast hitscan/projectile (R301-style capsule tracer)
    Rocket,    ///< Slow arcing projectile (ribbon trail)
    RailGun,   ///< Hitscan energy weapon (beam + lightning arcs)
    EnergyGun, ///< Fast hitscan energy burst
};

enum class WeaponSlot : uint8_t
{
    PRIMARY,
    SECONDARY,
    TERTIARY,
};

/// @brief Struct that defines this weapon's type, cooldown, and ammo.
struct GunInstance
{
    WeaponType type = WeaponType::Rifle;
    int totalAmmo = 0;
    int currentMagAmmo = 0;
    float fireCooldown = 0.f;
};

/// @brief Component attached to armed entities (players).
struct WeaponState
{
    GunInstance primary;
    GunInstance secondary;
    GunInstance tertiary;
    WeaponSlot current = WeaponSlot::PRIMARY; ///< Currently equipped weapon slot.
};
