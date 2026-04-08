#pragma once

/// @brief All physics tuning values in one place.
///
/// **Units:** Quake units (1 unit ≈ 1 inch), Y-up coordinate system.
///
/// Starting values target a Titanfall-to-Quake movement feel.
/// Tune iteratively — `k_gravity` and `k_jumpSpeed` must always be tuned together:
/// `jump height = k_jumpSpeed² / (2 × k_gravity)`.
namespace physics
{

// Gravity & jumping
constexpr float k_gravity = 1000.0f;  ///< Downward acceleration (units/s²). Faster than real-world for snappy arcs.
constexpr float k_jumpSpeed = 380.0f; ///< Initial upward velocity on jump (units/s). Gives apex ≈ 72 units (~6 ft).

// Ground movement
constexpr float k_maxGroundSpeed = 400.0f; ///< Maximum horizontal speed on ground (units/s).
constexpr float k_groundAccel = 15.0f;     ///< Ground acceleration constant. Higher = reaches max speed faster.

// Air movement
/// @brief Air acceleration constant. Higher than Quake (0.7) for Titanfall-style air control.
constexpr float k_airAccel = 2.0f;
/// @brief Wish-speed cap in air (units/s). Does NOT cap total speed — existing momentum is preserved.
constexpr float k_airMaxSpeed = 30.0f;

// Friction
constexpr float k_friction = 4.0f;    ///< Ground friction coefficient (Quake default).
constexpr float k_stopSpeed = 150.0f; ///< Friction is amplified below this speed for a crisp stop.

// Collision
constexpr float k_overbounceWall = 1.001f; ///< Separation impulse for walls/ceilings; prevents corner-sticking.
constexpr float k_overbounceFloor = 1.0f;  ///< Floor overbounce — exactly 1.0 means no bounce.

// Geometry
constexpr float k_stepHeight = 18.0f; ///< Maximum obstacle height auto-stepped over without jumping (units).

} // namespace physics
