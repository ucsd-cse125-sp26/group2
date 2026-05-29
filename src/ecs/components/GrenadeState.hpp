/// @file GrenadeState.hpp
/// @brief Player grenade equipment state.

#pragma once

#include "ecs/components/GrenadeConfig.hpp"
#include "ecs/components/WeaponConfig.hpp"

#include <array>
#include <cstddef>

const int defaultGrenadeStartAmount = 2;

struct GrenadeState
{
    WeaponType selected = WeaponType::HEGrenade;
    std::array<int, kGrenadeTypeCount> ammo{};
    float cooldown = 0.0f;
};

inline GrenadeState makeDefaultGrenadeState()
{
    GrenadeState state{};
    state.selected = WeaponType::HEGrenade;
    for (std::size_t i = 0; i < kGrenadeTypes.size(); ++i) {
        state.ammo[i] = defaultGrenadeStartAmount;
    }
    return state;
}

inline int& grenadeAmmo(GrenadeState& state, WeaponType type)
{
    return state.ammo[grenadeTypeIndex(type)];
}

inline int grenadeAmmo(const GrenadeState& state, WeaponType type)
{
    return state.ammo[grenadeTypeIndex(type)];
}
