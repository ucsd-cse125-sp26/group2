/// @file DroppedWeapon.hpp
/// @brief In-world weapon-pickup entity dropped when a player dies.

#pragma once

#include "ecs/components/WeaponState.hpp"

/// @brief ECS component: one-shot weapon pickup spawned at a player's death position.
///
/// Snapshots the slot's GunInstance ammo at death; pickup grants those exact
/// ammo counts (no full-mag refill like world spawners). Lifetime is bounded
/// by `despawnTimer`; pickup or expiry destroys the entity.
struct DroppedWeapon
{
    WeaponType type = WeaponType::Rifle; ///< Weapon type from the slot's GunInstance.
    int totalAmmo = 0;                   ///< Reserve ammo at death.
    int currentMagAmmo = 0;              ///< Ammo in the magazine at death.
    float despawnTimer = 0.0f;           ///< Seconds until the entity is destroyed.
};
