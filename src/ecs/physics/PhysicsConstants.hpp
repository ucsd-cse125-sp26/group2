/// @file PhysicsConstants.hpp
/// @brief All physics tuning constants in one place.

#pragma once

/// @brief All physics tuning values in one place.
///
/// **Units:** Quake units (1 unit ≈ 1 inch), Y-up coordinate system.
///
/// Starting values target a Titanfall-to-Quake movement feel.
/// Tune iteratively — `k_gravity` and `k_jumpSpeed` must always be tuned together:
/// `jump height = k_jumpSpeed^2 / (2 × k_gravity)`.
namespace physics
{

// Gravity & jumping
constexpr float k_gravity = 1000.0f;  ///< Downward acceleration (units/s^2). Faster than real-world for snappy arcs.
constexpr float k_jumpSpeed = 330.0f; ///< Initial upward velocity on jump (units/s). Gives apex ~ 54 units (~4.5 ft).

// Ground movement
// Ground wish speed is stance-dependent — see tms::k_walkSpeed / k_sprintSpeed / k_crouchSpeed
// and systems::currentWishSpeed() which selects between them.
constexpr float k_groundAccel = 10.0f; ///< Ground acceleration constant. Higher = reaches max speed faster.

// Air movement
constexpr float k_airAccel =
    700.0f; ///< Air acceleration constant. Higher than Quake (0.7) for Titanfall-style air control.
constexpr float k_airMaxSpeed = 30.0f; ///< Wish-speed FLOOR in air (units/s). The wish-speed cap once horizontal speed
                                       ///< exceeds k_airWishCurveTop. Preserves classic Quake strafe-jump physics at
                                       ///< speed. Does NOT cap total speed — existing momentum is preserved.

// Air wish-speed curve (lets you recover from low/zero air speed without breaking strafe-jump).
// Per-tick gain in wishDir is bounded by wishSpeed when below it, so this also bounds the
// initial "kick" from a stationary air state. Larger k_airMaxWishLowSpeed = stronger recovery
// but feels more teleporty at exactly the moment you start pressing a direction in air.
constexpr float k_airMaxWishLowSpeed = 120.0f; ///< Wish-speed CEILING in air (units/s) when stationary.
constexpr float k_airWishCurveTop = 250.0f;    ///< Horizontal speed (u/s) at which the curve
                                               ///< plateaus at k_airMaxSpeed.
constexpr float k_airWishCurveExponent = 0.4f; ///< Power-curve exponent for wish-speed falloff
                                               ///< (<1 = sharp early drop). t' = pow(t, e).

// Friction
constexpr float k_friction = 7.5f; ///< Ground friction coefficient. Higher = crisper stops, easier-to-track movement.
constexpr float k_stopSpeed = 150.0f; ///< Friction is amplified below this speed for a crisp stop.

// Collision
constexpr float k_overbounceWall = 1.001f; ///< Separation impulse for walls/ceilings; prevents corner-sticking.
constexpr float k_overbounceFloor = 1.0f;  ///< Floor overbounce — exactly 1.0 means no bounce.

// Geometry
constexpr float k_stepHeight = 18.0f; ///< Maximum obstacle height auto-stepped over without jumping (units).

} // namespace physics
