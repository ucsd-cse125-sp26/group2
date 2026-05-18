/// @file AbilityTuning.hpp
/// @brief Shared tuning and metadata helpers for selectable abilities.

#pragma once

#include "ecs/components/AbilityState.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/TitanfallConstants.hpp"

namespace abilities
{

constexpr float k_dashCooldown = 4.0f;
constexpr float k_dashSpeed = 1900.0f;
constexpr float k_dashLift = 90.0f;

constexpr float k_recallCooldown = 12.0f;

inline constexpr float cooldownFor(AbilityType type)
{
    switch (type) {
    case AbilityType::Dash:
        return k_dashCooldown;
    case AbilityType::Grapple:
        return tms::k_grappleCooldown;
    case AbilityType::Gravity:
        return physics::k_gravityFlipCooldown;
    case AbilityType::Recall:
        return k_recallCooldown;
    case AbilityType::None:
    default:
        return 0.0f;
    }
}

} // namespace abilities
