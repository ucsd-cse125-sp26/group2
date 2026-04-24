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
    bool isBeam = false;        ///< True for continuous beam weapons (no per-shot cooldown).
    bool isCharge = false;      ///< True for charge weapons (hold to charge, release to fire).
    float dps = 0.0f;           ///< Damage per second (beam weapons only; discrete weapons use `damage`).
    float ammoPerSecond = 0.0f; ///< Ammo drain rate (beam weapons only).
    float chargeDamage = 0.0f;  ///< Damage dealt on release (charge weapons only).
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
            .fireCooldown = 0.30f,
            .magazineSize = 8,
            .defaultAmmoCapacity = 32,
            .damage = 60.0f,
            .hitscan = true,
            .isCharge = true,
            .chargeDamage = 150.0f,
        }, // RailGun (charge sniper)
        WeaponConfig{
            .fireCooldown = 0.0f,
            .magazineSize = 200,
            .defaultAmmoCapacity = 200,
            .damage = 5.0f,
            .hitscan = true,
            .isBeam = true,
            .dps = 80.0f,
            .ammoPerSecond = 20.0f,
        }, // EnergyGun (Zarya beam)
    }};

    return k_kWeaponConfigs[static_cast<std::size_t>(type)];
}