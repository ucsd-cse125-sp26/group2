/// @file GrenadeConfig.hpp
/// @brief Per-grenade-type tuning data (throw, flight, detonation).
///
/// Single source of truth for grenade tuning. Each WeaponType in
/// {HEGrenade, Molotov, Sticky} maps to one row. Add a new grenade by:
///   1. appending an entry to WeaponType
///   2. appending a row to k_grenadeConfigs below.

#pragma once

#include "WeaponState.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>

/// @brief How a grenade detonates.
enum class GrenadeDetonationKind : uint8_t
{
    Explosion, ///< queueExplosion() with damage + knockback (HE, Sticky).
    FireField, ///< Spawn a FireField entity for damage-over-time (Molotov).
};

/// @brief All tuning for one grenade type.
struct GrenadeConfig
{
    // Throw mechanics
    float throwSpeed = 1500.0f;     ///< Initial speed along throw direction (u/s).
    float throwPitchOffset = 0.14f; ///< Slight upward bias on eye dir (rad). Keep small so the grenade
                                    ///< visibly tracks the crosshair; gravity supplies the arc.
    float throwCooldown = 0.4f;     ///< Min seconds between throws.

    // Flight physics
    float fuseTime = -1.0f;         ///< Seconds; <0 means impact-detonate (no fuse).
    float bounceRestitution = 0.0f; ///< 0 = no bounce, 0.5 = lossy, 1.0 = elastic.
    bool sticky = false;            ///< If true, freezes velocity on first surface hit.
    float maxLifeTime = 8.0f;       ///< Hard timeout safety (s).

    // Detonation
    GrenadeDetonationKind detonation = GrenadeDetonationKind::Explosion;
    // — Explosion params (used when detonation == Explosion)
    float damage = 0.0f;
    float explosionRadius = 0.0f;
    float damageFalloffExp = 1.0f;
    float selfDamageMult = 0.4f;
    float maxKnockback = 0.0f;
    float knockbackFalloffExp = 1.0f;
    // — FireField params (used when detonation == FireField)
    float fireRadius = 0.0f;
    float fireDuration = 0.0f;
    float fireDps = 0.0f;

    // Cosmetic
    int modelId = 1;                     ///< Reuses rocket model for v1.
    glm::vec3 tint = {1.0f, 1.0f, 1.0f}; ///< RGB multiplier for projectile rendering.
};

/// @brief Duration of the throw "wind-up": the gun dips (client viewmodel) and
/// firing is locked out for this long after a throw. Shorter than throwCooldown.
inline constexpr float kGrenadeThrowAnimTime = 0.5f;

inline constexpr std::array<WeaponType, 3> kGrenadeTypes = {
    WeaponType::HEGrenade,
    WeaponType::Molotov,
    WeaponType::Sticky,
};
inline constexpr std::size_t kGrenadeTypeCount = kGrenadeTypes.size();

/// @brief True if `type` is a grenade (covered by getGrenadeConfig).
inline bool isGrenadeType(WeaponType type)
{
    for (WeaponType grenadeType : kGrenadeTypes) {
        if (type == grenadeType) {
            return true;
        }
    }
    return false;
}

inline std::size_t grenadeTypeIndex(WeaponType type)
{
    for (std::size_t i = 0; i < kGrenadeTypes.size(); ++i) {
        if (kGrenadeTypes[i] == type) {
            return i;
        }
    }

    assert(false && "grenadeTypeIndex called on non-grenade WeaponType");
    return 0;
}

inline WeaponType grenadeTypeAt(std::size_t index)
{
    if (index >= kGrenadeTypes.size()) {
        index = 0;
    }
    return kGrenadeTypes[index];
}

inline constexpr const char* grenadeTypeName(WeaponType type)
{
    switch (type) {
    case WeaponType::HEGrenade:
        return "FRAG";
    case WeaponType::Molotov:
        return "MOLOTOV";
    case WeaponType::Sticky:
        return "STICKY";
    default:
        return "GRENADE";
    }
}

/// @brief Weapon-slot type compatibility predicate for pickup guards.
///
/// Grenades are equipped through GrenadeState, not WeaponState, so weapon slots
/// reject grenade types.
inline bool canAcceptType(WeaponSlot /*slot*/, WeaponType type)
{
    return !isGrenadeType(type);
}

/// @brief Returns the config for a grenade WeaponType.
/// @note Behavior is undefined if `type` is not a grenade type.
inline const GrenadeConfig& getGrenadeConfig(WeaponType type)
{
    assert(isGrenadeType(type) && "getGrenadeConfig requires a grenade WeaponType");
    // Pin the index calc below (type - HEGrenade) to the WeaponType ordering.
    // If a future edit reorders WeaponType, this fires at compile time instead of
    // silently miswiring the table.
    static_assert(static_cast<std::size_t>(WeaponType::Molotov) == static_cast<std::size_t>(WeaponType::HEGrenade) + 1);
    static_assert(static_cast<std::size_t>(WeaponType::Sticky) == static_cast<std::size_t>(WeaponType::HEGrenade) + 2);
    static constexpr std::array<GrenadeConfig, 3> k_grenadeConfigs{{
        // HEGrenade
        GrenadeConfig{
            .throwSpeed = 1500.0f,
            .throwPitchOffset = 0.14f,
            .throwCooldown = 1.0f,
            .fuseTime = 3.0f,
            .bounceRestitution = 0.5f,
            .sticky = false,
            .maxLifeTime = 8.0f,
            .detonation = GrenadeDetonationKind::Explosion,
            .damage = 120.0f,
            .explosionRadius = 200.0f,
            .damageFalloffExp = 2.5f,
            .selfDamageMult = 0.4f,
            .maxKnockback = 1100.0f, // impulse-grade knockback folded into the HE frag
            .knockbackFalloffExp = 1.5f,
            .modelId = 1,
            .tint = {0.4f, 1.0f, 0.4f}, // green
        },
        // Molotov
        GrenadeConfig{
            .throwSpeed = 1200.0f,
            .throwPitchOffset = 0.20f,
            .throwCooldown = 1.0f,
            .fuseTime = -1.0f, // impact-detonate
            .bounceRestitution = 0.0f,
            .sticky = false,
            .maxLifeTime = 5.0f,
            .detonation = GrenadeDetonationKind::FireField,
            .fireRadius = 250.0f,
            .fireDuration = 5.0f,
            .fireDps = 30.0f,
            .modelId = 1,
            .tint = {1.0f, 0.5f, 0.1f}, // orange
        },
        // Sticky
        GrenadeConfig{
            .throwSpeed = 1500.0f,
            .throwPitchOffset = 0.14f,
            .throwCooldown = 1.0f,
            .fuseTime = 2.0f, // arms once it sticks to a surface or player
            .bounceRestitution = 0.0f,
            .sticky = true,
            .maxLifeTime = 8.0f,
            .detonation = GrenadeDetonationKind::Explosion,
            .damage = 100.0f,
            .explosionRadius = 220.0f,
            .damageFalloffExp = 2.0f,
            .selfDamageMult = 0.4f,
            .maxKnockback = 400.0f,
            .knockbackFalloffExp = 2.0f,
            .modelId = 1,
            .tint = {0.4f, 0.6f, 1.0f}, // blue
        },
    }};

    const std::size_t k_idx = static_cast<std::size_t>(type) - static_cast<std::size_t>(WeaponType::HEGrenade);
    return k_grenadeConfigs[k_idx];
}
