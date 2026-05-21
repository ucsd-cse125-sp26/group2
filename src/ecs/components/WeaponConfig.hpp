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
    float maxChargeTime = 0.0f; ///< in seconds
};

struct ProjectileConfig
{
    int modelId = 0;
    float initialSpeed = 0.0f;
    float scale = 1.0f;
    CollisionShape shape = CollisionShape{.halfExtents = {5.0f, 5.0f, 5.0f}};
    float maxLifeTime = 5.0f;
    float explosionRadius = 0.0f;
    /// @brief Damage falloff curve exponent. `damage = maxDamage * pow(1 - d/r, exponent)`.
    /// 1.0 = linear (uniform falloff); 2.0 = quadratic; 3.0 = cubic (sharp falloff —
    /// direct hits lethal, near misses chip damage).
    float explosionFalloffExponent = 1.0f;
    /// @brief Damage scale applied when the rocket's owner is the victim (self-damage).
    /// 1.0 = full damage to self; 0.4 = rocket-jump friendly (40% self-damage).
    float selfDamageMultiplier = 1.0f;
    /// @brief Peak knockback velocity (u/s) imparted at the epicenter. 0 = no knockback.
    /// Applied additively to victim's Velocity, in the direction away from the blast.
    float maxKnockback = 0.0f;
    /// @brief Knockback falloff exponent (same form as `explosionFalloffExponent`).
    /// Typically gentler than damage falloff so the push reaches slightly further than
    /// the lethal zone — rewards near misses for movement plays.
    float knockbackFalloffExponent = 1.0f;
};

/// @brief Returns the gameplay config for a weapon type.
inline const WeaponConfig& getWeaponConfig(WeaponType type)
{
    static constexpr std::array<WeaponConfig, 7> k_kWeaponConfigs{{
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
            .initialProjectileSpeed = 3000.0f,
            .explosive = true,
        }, // Rocket
        WeaponConfig{
            .fireCooldown = 0.5f,
            .magazineSize = 8,
            .defaultAmmoCapacity = 32,
            .damage = 50.0f,
            .hitscan = true,
            .initialProjectileSpeed = 0.0f,
            .explosive = false,
            .isCharge = true,
            .chargeDamage = 80.0f,
            .maxChargeTime = 1.0f,
        }, // RailGun
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
        }, // EnergyGun
        WeaponConfig{
            .fireCooldown = 0.4f,
            .magazineSize = 1,
            .defaultAmmoCapacity = 99,
            .damage = 0.0f,
            .hitscan = false,
            .initialProjectileSpeed = 1500.0f, // overridden by GrenadeConfig.throwSpeed at spawn
            .explosive = false,
        },                                     // HEGrenade
        WeaponConfig{
            .fireCooldown = 0.4f,
            .magazineSize = 1,
            .defaultAmmoCapacity = 99,
            .damage = 0.0f,
            .hitscan = false,
            .initialProjectileSpeed = 1200.0f,
            .explosive = false,
        }, // Molotov
        WeaponConfig{
            .fireCooldown = 0.4f,
            .magazineSize = 1,
            .defaultAmmoCapacity = 99,
            .damage = 0.0f,
            .hitscan = false,
            .initialProjectileSpeed = 1500.0f,
            .explosive = false,
        }, // Impulse
    }};

    return k_kWeaponConfigs[static_cast<std::size_t>(type)];
}

/// @brief Returns the projectile config for a weapon type.
inline const ProjectileConfig& getProjectileConfig(WeaponType type)
{
    static constexpr std::array<ProjectileConfig, 7> k_kProjectileConfigs{{
        ProjectileConfig{}, // Rifle
        ProjectileConfig{
            .modelId = 1,
            .initialSpeed = 0.0f,
            .scale = 1.0f,
            .shape = CollisionShape{.halfExtents = {5.0f, 5.0f, 5.0f}},
            .maxLifeTime = 5.0f,
            .explosionRadius = 250.0f,
            .explosionFalloffExponent = 3.0f, // Cubic: direct hits 1-shot, ~2m away ≈ 65 dmg, ~3m ≈ chip.
            .selfDamageMultiplier = 0.4f,     // 40% self-damage so rocket jumps don't suicide.
            .maxKnockback = 800.0f,           // Feet-rocket pop scaled against k_jumpSpeed=660; retune if too soft.
            .knockbackFalloffExponent = 2.0f, // Quadratic: push reaches further than damage.
        },                                    // Rocket
        ProjectileConfig{},                   // RailGun
        ProjectileConfig{},                   // EnergyGun
        ProjectileConfig{},                   // HEGrenade — flight params come from GrenadeConfig
        ProjectileConfig{},                   // Molotov
        ProjectileConfig{},                   // Impulse
    }};

    return k_kProjectileConfigs[static_cast<std::size_t>(type)];
}
