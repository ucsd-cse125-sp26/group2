/// @file WeaponSpawner.hpp
/// @brief Weapon spawner information.

#pragma once
#include "WeaponState.hpp"

/// @brief ECS component: world weapon pickup point with respawn cooldown.
struct WeaponSpawner
{
    WeaponType type = WeaponType::Rifle; ///< Type of weapon this spawner provides.
    float spawnCooldown = 0;             ///< Seconds remaining before the weapon reappears.
    bool hasWeapon = false;              ///< True if a weapon is available for pickup.
};
