/// @file WeaponConfig.hpp
/// @brief Static gameplay tuning data for each weapon type.

#pragma once

#include "CollisionShape.hpp"
#include "ecs/components/Projectile.hpp"

#include <array>
#include <cstddef>

/// @brief Immutable gameplay stats for a weapon type.
struct WeaponConfig
{
    float fireCooldown = 0.1f;
    int magazineSize = 0;
    int defaultAmmoCapacity = 0;
    float damage = 0.0f;
    bool hitscan = true;
    float initialProjectileSpeed = 0.0f;
    bool explosive = false;
    bool isBeam = false;        ///< True for continuous beam weapons (no per-shot cooldown).
    bool isCharge = false;      ///< True for charge weapons (hold to charge, release to fire).
    float dps = 0.0f;           ///< Damage per second (beam weapons only; discrete weapons use `damage`).
    float ammoPerSecond = 0.0f; ///< Ammo drain rate (beam weapons only).
    float chargeDamage = 0.0f;  ///< Damage dealt on release (charge weapons only).
};

struct ProjectileConfig
{
    int modelId = 0;
    float initialSpeed = 0.0f;
    float scale = 1.0f;
    CollisionShape shape = CollisionShape{.halfExtents = {5.0f, 5.0f, 5.0f}};
    float maxLifeTime = 5.0f;
    float explosionRadius = 0.0f;
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
            .initialProjectileSpeed = 0.0f,
            .explosive = false,
        }, // Rifle
        WeaponConfig{
            .fireCooldown = 1.0f,
            .magazineSize = 4,
            .defaultAmmoCapacity = 12,
            .damage = 200.0f,
            .hitscan = false,
            .initialProjectileSpeed = 500.0f,
            .explosive = true,
        }, // Rocket
        WeaponConfig{
            .fireCooldown = 0.50f,
            .magazineSize = 5,
            .defaultAmmoCapacity = 20,
            .damage = 60.0f,
            .hitscan = true,
            .initialProjectileSpeed = 0.0f,
            .explosive = false,
            .isCharge = true,
            .chargeDamage = 150.0f,
        }, // RailGun (charge sniper)
        WeaponConfig{
            .fireCooldown = 0.0f,
            .magazineSize = 200,
            .defaultAmmoCapacity = 200,
            .damage = 5.0f,
            .hitscan = true,
            .initialProjectileSpeed = 0.0f,
            .explosive = false,
            .isBeam = true,
            .dps = 80.0f,
            .ammoPerSecond = 20.0f,
        }, // EnergyGun (Zarya beam)
    }};

    return k_kWeaponConfigs[static_cast<std::size_t>(type)];
}

/// @brief Returns the projectile config for a weapon type.
inline const ProjectileConfig& getProjectileConfig(WeaponType type)
{
    static constexpr std::array<ProjectileConfig, 4> k_kProjectileConfigs{{
        ProjectileConfig{}, // Rifle
        ProjectileConfig{
            .modelId = 1,
            .initialSpeed = 0.0f,
            .scale = 1.0f,
            .shape = CollisionShape{.halfExtents = {5.0f, 5.0f, 5.0f}},
            .maxLifeTime = 5.0f,
            .explosionRadius = 175.0f,
        },                  // Rocket
        ProjectileConfig{}, // RailGun
        ProjectileConfig{}, // EnergyGun
    }};

    return k_kProjectileConfigs[static_cast<std::size_t>(type)];
}
