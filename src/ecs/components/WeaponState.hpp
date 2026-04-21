/// @file WeaponState.hpp
/// @brief Weapon state component for armed entities.

#pragma once

#include "Projectile.hpp"

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
    WeaponSlot current = WeaponSlot::PRIMARY; ///< Currently equipped weapon slot.
};



