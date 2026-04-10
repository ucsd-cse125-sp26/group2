#pragma once

#include "Projectile.hpp"

/// @brief Component attached to armed entities (players, bots).
struct WeaponState
{
    WeaponType current = WeaponType::Rifle;
    float fireCooldown = 0.f; ///< Counts down toward 0 each frame (seconds).
    int ammo = 30;
};
