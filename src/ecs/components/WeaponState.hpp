/// @file WeaponState.hpp
/// @brief Weapon state component for armed entities.

#pragma once

#include "Projectile.hpp"

/// @brief Component attached to armed entities (players, bots).
struct WeaponState
{
    WeaponType current = WeaponType::Rifle; ///< Currently equipped weapon type.
    float fireCooldown = 0.f;               ///< Counts down toward 0 each frame (seconds).
    int ammo = 30;                          ///< Remaining ammunition for the current weapon.
};
