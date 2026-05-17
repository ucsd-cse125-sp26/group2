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

/// @brief `dot(surfaceNormal, up)` threshold above which a surface counts as walkable
/// floor.  Cos(45.6°) ≈ 0.7 — surfaces steeper than this are walls, not floors.
constexpr float k_floorAngleCos = 0.7f;

/// @brief Distance the ground probe extends below the capsule foot to snap to
/// descending slopes / steps.  Allows a grounded player to follow stair-downs
/// and slope-downs without going airborne for a tick.
constexpr float k_groundSnapDistance = 8.0f;

/// @brief Maximum radius of the emergency-unstick free-space search.  When
/// per-pass depen fails to resolve penetration, we probe outward up to this
/// far in cardinal directions for a clear teleport target.
constexpr float k_emergencyUnstickRadius = 64.0f;

/// @brief Maximum sequential passes the deepest-first capsule depen attempts
/// before falling through to emergency unstick.
constexpr int k_maxDepenPasses = 6;

// Gravity flip
constexpr float k_gravityFlipCooldown = 0.5f; ///< Minimum time between gravity flips (s).

// Sub-stepping (Phase C of physics-future-path.md)
//
// The 128 Hz bump loop is conservative-advancement-via-sweep-TOI: each clip
// iteration advances to the swept time-of-impact, never penetrating past
// the swept-shape's safety margin.  This is exact for our plane / brush
// queries and the capsule-vs-triangle Voronoi sweep, and bounded
// conservative for capsule-vs-box/cyl/sphere.
//
// At normal player speeds (≤ 800 u/s, ≤ 6.25 u/tick at 128 Hz) and a 16 u
// capsule radius, one sweep per tick comfortably catches any thin feature.
// The failure mode shows up at extreme velocities (grapple-hook yank,
// explosion knockback, scripted teleports): when `|v|·dt` exceeds the
// shape's safety margin, the sweep's per-tick swept distance can exceed
// the size of thin geometry features, risking tunneling.
//
// Sub-stepping splits the tick into N equal substeps when the projected
// motion exceeds `k_substepSafetyRatio · min_shape_radius`.  Each
// substep runs the full bump loop with `dt/N`.  Determinism is preserved
// because both client and server compute the same `N` from the same
// inputs.  Depen runs once per tick (its idempotence + the bump loop's
// pushback handle the rest); slope-stick and ground-probe also run once
// at the end of the tick.

constexpr bool k_enableSubstepping = true; ///< Master toggle for Phase-C sub-stepping.
constexpr float k_substepSafetyRatio = 0.5f; ///< Sub-step when `|v|·dt > min_shape_radius · this`.
constexpr int k_maxSubsteps = 8;             ///< Clamp on sub-step count to bound worst-case cost.

} // namespace physics
