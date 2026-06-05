/// @file BeamLockState.hpp
/// @brief Server-side lock-on state for auto-lock beam weapons (Tesla Cannon).
///
/// Tracks which target the shooter's auto-lock beam is currently fixed on and
/// for how long, so damage can ramp while the lock is held and reset the instant
/// it breaks. This is server-authoritative gameplay state and is intentionally
/// NOT part of the network-synced component set — clients only need `BeamState`
/// (origin + hitPoint) to draw the beam.

#pragma once

#include <entt/entity/entity.hpp>

/// @brief Per-shooter lock state for a Winston-style auto-lock beam.
struct BeamLockState
{
    entt::entity target{entt::null}; ///< Currently locked enemy, or null if none.
    float duration{0.0f};            ///< Seconds the current target has been continuously locked.
    /// @brief Seconds since the target was last in the cone. Used as a short
    /// forgiveness window so a brief loss of lock (target strafes, peeks behind
    /// cover, or crosshair micro-jitter) does not reset the damage ramp.
    float graceTimer{0.0f};
};
