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
    bool isBeam = false;             ///< True for continuous beam weapons (no per-shot cooldown).
    bool isCharge = false;           ///< True for charge weapons (hold to charge, release to fire).
    float dps = 0.0f;                ///< Damage per second (beam weapons only; discrete weapons use `damage`).
    float ammoPerSecond = 0.0f;      ///< Ammo drain rate (beam weapons only).
    float chargeDamage = 0.0f;       ///< Damage dealt on release (charge weapons only).
    float maxChargeTime = 0.0f;      ///< in seconds
    float reloadTime = 0.0f;         ///< Time to complete a reload, in seconds.

    bool semiAuto = false;           ///< If true, fire requires trigger release between shots (pump/burst weapons).

    int recoilFreeShots = 0;         ///< Shots before recoil kicks in (legacy formula path).
    float recoilPitchPerShot = 0.0f; ///< Upward kick per shot (legacy formula path).
    float recoilYawPerShot = 0.0f;   ///< Horizontal swing strength (legacy formula path).
    float recoilRampShots = 1.0f;    ///< Curve shape / stem length (legacy formula path).
    float recoilRecovery = 0.0f;     ///< Heat decay per second (shared by both paths).

    /// @brief Closed-form pattern (R-301-style) scale; rad per pattern unit.
    /// When > 0, the fire path evaluates the closed-form H(n)/V(n) function below
    /// instead of the legacy `sin+exp` formula. The pattern's domain is [1, 28]
    /// (user-provided shape), and we resample it across `magazineSize` shots — so
    /// firing the full mag walks the entire pattern from n=1 → n=28 with finer
    /// granularity than the original 28-shot Apex pattern. Pattern V units total
    /// 100 across the mag, so total vertical climb = 100 * scale radians.
    float recoilPatternScale = 0.0f;
};

/// @brief Closed-form 5-segment R-301 recoil pattern (horizontal, right-positive).
/// Defined over n in [1, 28]; returns cumulative H in pattern units.
inline float recoilPatternH_R301(float n)
{
    if (n <= 10.0f) {
        const float q = n - 1.0f;
        return 3.4047f * q - 0.2173f * q * q;
    }
    if (n <= 15.0f) {
        const float q = n - 10.0f;
        return 13.0435f + 2.1919f * q + 0.2681f * q * q;
    }
    if (n <= 20.0f) {
        const float q = n - 15.0f;
        return 30.7065f - 2.1566f * q - 0.3404f * q * q;
    }
    if (n <= 22.0f) {
        const float q = n - 20.0f;
        return 11.4130f - 2.6123f * q;
    }
    const float q = n - 22.0f;
    return 6.1885f + 1.9224f * q + 0.2776f * q * q;
}

/// @brief Closed-form 5-segment R-301 recoil pattern (vertical, up-positive).
/// Defined over n in [1, 28]; returns cumulative V in pattern units.
inline float recoilPatternV_R301(float n)
{
    if (n <= 10.0f) {
        const float q = n - 1.0f;
        return 4.0792f * q + 0.09485f * q * q;
    }
    if (n <= 15.0f) {
        const float q = n - 10.0f;
        return 44.3954f + 5.0237f * q - 0.2764f * q * q;
    }
    if (n <= 20.0f) {
        const float q = n - 15.0f;
        return 62.6046f + 4.5740f * q - 0.5132f * q * q;
    }
    if (n <= 22.0f) {
        const float q = n - 20.0f;
        return 72.6457f + 4.4688f * q;
    }
    const float q = n - 22.0f;
    return 81.5833f + 4.1890f * q - 0.1866f * q * q;
}

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
    static constexpr std::array<WeaponConfig, 8> k_kWeaponConfigs{{
        WeaponConfig{
            .fireCooldown = 0.10f,
            .magazineSize = 50,
            .defaultAmmoCapacity = 500,
            .damage = 15.0f,
            .hitscan = true,
            .initialProjectileSpeed = 0.0f,
            .explosive = false,
            .reloadTime = 1.25f,
            // R-301 closed-form pattern (user-provided 5-segment polynomial in
            // WeaponConfig.hpp). Resampled across the 50-bullet mag so the full
            // pattern domain [1, 28] walks shot 1 → 50. recoilRecovery still
            // controls heat decay between bursts (resets the pattern).
            // The legacy per-shot fields are zeroed; the pattern path takes
            // priority when recoilPatternScale > 0.
            .recoilFreeShots = 0,
            .recoilPitchPerShot = 0.0f,
            .recoilYawPerShot = 0.0f,
            .recoilRampShots = 1.0f,
            .recoilRecovery = 30.0f,
            .recoilPatternScale = 0.0025f, // ~14.3° total vertical at full mag (100 units).
        },                                 // Rifle
        WeaponConfig{
            .fireCooldown = 1.0f,
            .magazineSize = 4,
            .defaultAmmoCapacity = 12,
            .damage = 200.0f,
            .hitscan = false,
            .initialProjectileSpeed = 3000.0f,
            .explosive = true,
            .reloadTime = 2.5f,
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
            .reloadTime = 2.0f,
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
            .reloadTime = 2.0f,
        }, // EnergyGun
        WeaponConfig{
            // Peacekeeper-style pump shotgun: hitscan multi-pellet star spread.
            // The fire path in WeaponSystem.cpp checks `type == Shotgun` and loops
            // 9 raycasts in an asterisk pattern (1 center + 8 outer). Per-pellet
            // damage is `damage` below (so max body damage = 9 * damage).
            .fireCooldown = 0.9f,
            .magazineSize = 6,
            .defaultAmmoCapacity = 36,
            .damage = 10.0f, // per-pellet; 11 pellets → 110 body max, ~165 head.
            .hitscan = true,
            .initialProjectileSpeed = 0.0f,
            .explosive = false,
            .reloadTime = 2.5f,
            // semiAuto intentionally left false — shotgun auto-fires on cooldown
            // while LMB is held (matches charge rifle in normal mode). The
            // 0.9s fireCooldown is what gates the rate.
        }, // Shotgun
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
        }, // Sticky
    }};

    return k_kWeaponConfigs[static_cast<std::size_t>(type)];
}

/// @brief Returns the projectile config for a weapon type.
inline const ProjectileConfig& getProjectileConfig(WeaponType type)
{
    static constexpr std::array<ProjectileConfig, 8> k_kProjectileConfigs{{
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
        ProjectileConfig{},                   // Shotgun — hitscan, no projectile
        ProjectileConfig{},                   // HEGrenade — flight params come from GrenadeConfig
        ProjectileConfig{},                   // Molotov
        ProjectileConfig{},                   // Sticky
    }};

    return k_kProjectileConfigs[static_cast<std::size_t>(type)];
}
