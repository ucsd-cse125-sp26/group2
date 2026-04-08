#pragma once

// All physics tuning values in one place.
// Units: Quake units (1 unit ~= 1 inch).  Y-up coordinate system.
//
// These are starting values targeting a Titanfall-to-Quake movement feel.
// Tune iteratively — gravity and jumpSpeed must always be tuned together,
// as jump height = k_jumpSpeed^2 / (2 * k_gravity).
namespace physics
{
// ---- Gravity & jumping --------------------------------------------------
constexpr float k_gravity = 1000.0f;  // units/s^2  — faster than real for snappy arcs
constexpr float k_jumpSpeed = 380.0f; // units/s   — → apex ≈ 72 units (~6 feet)

// ---- Ground movement ----------------------------------------------------
constexpr float k_maxGroundSpeed = 400.0f; // units/s
constexpr float k_groundAccel = 15.0f;     // how quickly k_maxGroundSpeed is reached

// ---- Air movement -------------------------------------------------------
// k_airMaxSpeed caps the wish-speed in air; it does NOT cap total speed.
// Existing momentum (e.g. from a bhop) is always preserved — PM_Accelerate
// only adds velocity toward the wish direction, it never subtracts from it.
constexpr float k_airAccel = 2.0f;     // higher than Quake (0.7) for Titanfall-ish air control
constexpr float k_airMaxSpeed = 30.0f; // wish speed cap in air

// ---- Friction -----------------------------------------------------------
constexpr float k_friction = 4.0f;    // Quake default
constexpr float k_stopSpeed = 150.0f; // friction amplified below this speed → snappy stop

// ---- Collision ----------------------------------------------------------
// overbounce > 1.0 adds a tiny separation impulse to prevent corner-sticking.
constexpr float k_overbounceWall = 1.001f; // for walls and ceilings
constexpr float k_overbounceFloor = 1.0f;  // for floors — no bounce

// ---- Geometry -----------------------------------------------------------
constexpr float k_stepHeight = 18.0f; // max height auto-stepped over without jumping
} // namespace physics
