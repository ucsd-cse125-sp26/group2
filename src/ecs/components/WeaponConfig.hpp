/// @file WeaponConfig.hpp
/// @brief Static gameplay tuning data for each weapon type.

#pragma once

#include "ecs/components/Projectile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

/// @brief Immutable gameplay stats for a weapon type.
struct WeaponConfig
{
    float fireCooldown = 0.1f;
    int magazineSize = 0;
    int defaultAmmoCapacity = 0;
    float damage = 0.0f;
    bool hitscan = true;
};

/// @brief Returns the gameplay config for a weapon type.
inline const WeaponConfig& getWeaponConfig(WeaponType type)
{
    static constexpr std::array<WeaponConfig, 4> k_kWeaponConfigs{{
        WeaponConfig{
            .fireCooldown = 0.10f,
            .magazineSize = 50,
            .defaultAmmoCapacity = 500,
            .damage = 15.0f,
            .hitscan = true,
        }, // Rifle
        WeaponConfig{
            .fireCooldown = 0.80f,
            .magazineSize = 2,
            .defaultAmmoCapacity = 10,
            .damage = 100.0f,
            .hitscan = false,
        }, // Rocket
        WeaponConfig{
            .fireCooldown = 0.50f,
            .magazineSize = 5,
            .defaultAmmoCapacity = 20,
            .damage = 60.0f,
            .hitscan = true,
        }, // RailGun
        WeaponConfig{
            .fireCooldown = 0.05f,
            .magazineSize = 200,
            .defaultAmmoCapacity = 200,
            .damage = 5.0f,
            .hitscan = true,
        }, // EnergyGun
    }};

    return k_kWeaponConfigs[static_cast<std::size_t>(type)];
}