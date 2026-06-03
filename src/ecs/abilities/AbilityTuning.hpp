/// @file AbilityTuning.hpp
/// @brief Shared tuning and metadata helpers for selectable abilities.

#pragma once

#include "ecs/components/AbilityState.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/TitanfallConstants.hpp"

namespace abilities
{

constexpr float k_dashCooldown = 1.5f; ///< Buffed (was 2.5/4.0) — dash much more often.
constexpr float k_dashSpeed = 3400.0f; ///< Buffed (was 2400/1900) — much faster, longer burst.
constexpr float k_dashLift = 180.0f; ///< Buffed (was 90) — more lift keeps the dash airborne so its momentum carries
                                     ///< past ground friction instead of bleeding off immediately.

constexpr float k_recallCooldown = 12.0f;

// ── Levitate (tier-1 updraft) ───────────────────────────────────────────────
// Toggle: press to activate, press again (or let the timer expire) to stop.
constexpr float k_levitateMaxDuration = 4.0f; ///< Max seconds of lift per activation.
constexpr float k_levitateCooldown = 6.0f;    ///< Cooldown after a levitate ends.
constexpr float k_levitateRiseSpeed = 650.0f; ///< Target upward speed (u/s) — strong updraft.
constexpr float k_levitateResponse = 8.0f;    ///< How fast vertical velocity eases toward the rise speed.

// ── Wallhack (tier-2 see-through-walls) ─────────────────────────────────────
constexpr float k_wallhackDuration = 5.0f;  ///< Seconds enemies are revealed.
constexpr float k_wallhackCooldown = 30.0f; ///< Cooldown after activation.

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
    case AbilityType::Levitate:
        return k_levitateCooldown;
    case AbilityType::Wallhack:
        return k_wallhackCooldown;
    case AbilityType::None:
    default:
        return 0.0f;
    }
}

} // namespace abilities
