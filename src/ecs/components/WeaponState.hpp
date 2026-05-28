/// @file WeaponState.hpp
/// @brief Weapon state component for armed entities.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// @brief Weapon type — determines tracer style, damage, sound, and impact effects.
enum class WeaponType : uint8_t
{
    Rifle,     ///< Fast hitscan/projectile (R301-style capsule tracer)
    Rocket,    ///< Slow arcing projectile (ribbon trail)
    RailGun,   ///< Hitscan energy weapon (beam + lightning arcs)
    EnergyGun, ///< Fast hitscan energy burst
    Shotgun,   ///< Peacekeeper-style pump: 9-pellet star spread, hitscan
    HEGrenade, ///< Bouncy grenade with 3s fuse, lethal explosion
    Molotov,   ///< Impact-detonate, leaves a fire field (damage over time)
    Impulse,   ///< Sticky 1s fuse, big knockback, no damage (movement tool)
};

/// @brief Identifier for one of the player's weapon slots.
///
/// `Count` is a sentinel used to size the slots array; never use it as a real slot value.
/// To add a new slot (e.g., melee), append before `Count` and update any per-slot policy
/// helpers (see canAcceptType in a future commit). The slot array auto-resizes.
enum class WeaponSlot : uint8_t
{
    PRIMARY,
    SECONDARY,
    Count, ///< Sentinel — must be last. Equals the number of valid slots.
};

/// @brief Struct that defines this weapon's type, cooldown, and ammo.
struct GunInstance
{
    WeaponType type = WeaponType::Rifle;
    int totalAmmo = 0;
    int currentMagAmmo = 0;
    float fireCooldown = 0.f;
    float chargeTime = 0.f;     ///< Accumulated charge time (charge weapons only).
    bool isReloading = false;   ///< True while reload is in progress
    float reloadTime = 0.f;     ///< Time remaining to complete a reload
    float recoilHeat = 0.f;     ///< Recoil heat (decays when not firing).
    float recoilIdleTime = 0.f; ///< Time off trigger before decay starts.
    bool firedSinceTriggerPress = false; ///< Semi-auto gate: blocks re-fire until trigger released.
};

/// @brief Component attached to armed entities (players).
///
/// Slots are stored in a fixed-size array indexed by `WeaponSlot`. Use the
/// `getSlot` / `getEquippedGun` helpers below — do NOT index `.slots` directly
/// from consumer code. This keeps the slot-ordering an implementation detail
/// of `WeaponState` and lets us add slots without touching every consumer.
struct WeaponState
{
    std::array<GunInstance, static_cast<std::size_t>(WeaponSlot::Count)> slots{};
    WeaponSlot current = WeaponSlot::PRIMARY; ///< Currently equipped slot.
};

/// @brief Access a specific slot on a WeaponState by enum.
inline GunInstance& getSlot(WeaponState& w, WeaponSlot s)
{
    return w.slots[static_cast<std::size_t>(s)];
}
inline const GunInstance& getSlot(const WeaponState& w, WeaponSlot s)
{
    return w.slots[static_cast<std::size_t>(s)];
}

/// @brief The slot the player is currently holding.
inline GunInstance& getEquippedGun(WeaponState& w)
{
    return getSlot(w, w.current);
}
inline const GunInstance& getEquippedGun(const WeaponState& w)
{
    return getSlot(w, w.current);
}
