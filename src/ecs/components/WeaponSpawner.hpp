/// @file WeaponSpawner.hpp
/// @brief Weapon spawner information.

#pragma once
#include "WeaponState.hpp"

struct WeaponSpawner
{
    WeaponType type = WeaponType::Rifle;
    float spawnCooldown = 0;
    bool hasWeapon = false;
};

