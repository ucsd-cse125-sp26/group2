/// @file GrenadeInventory.hpp
/// @brief Per-player grenade selection state.

#pragma once

#include "WeaponState.hpp"

/// @brief Tracks which grenade type the player has currently selected.
///
/// Counts/limits are intentionally absent in v1 — players have unlimited
/// grenades for testing. To add carry counts later, extend with a
/// `std::array<int, 3> remaining` indexed by (type - HEGrenade).
struct GrenadeInventory
{
    WeaponType currentType = WeaponType::HEGrenade;
};
