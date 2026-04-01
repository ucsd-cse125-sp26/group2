#pragma once
#include "../game/World.hpp"

#include <entt/entt.hpp>

// ---------------------------------------------------------------------------
// Quake / Source-derived physics constants
// ---------------------------------------------------------------------------

struct PhysicsConfig
{
    // Movement
    float maxSpeedGround = 320.0f;  // qu/s
    float maxSpeedAir    = 30.0f;   // wishspeed cap for air-strafe
    float accelGround    = 10.0f;   // sv_accelerate
    float accelAir       = 10.0f;   // sv_airaccelerate (same value, different wishspeed cap)
    float friction       = 5.5f;    // sv_friction (CS:GO ~5.2; higher = snappier stops)
    float stopSpeed      = 100.0f;  // sv_stopspeed (min speed for full friction)
    float maxVelocity    = 3500.0f; // sv_maxvelocity

    // Jumping
    float jumpSpeed = 270.0f; // vertical velocity added on jump (qu/s)
    bool autoBhop   = true;   // hold jump to bhop automatically

    // Walljump
    float wallJumpSpeed   = 270.0f; // vertical component
    float wallJumpLateral = 0.80f;  // fraction of velocity reflected off wall
    float wallCooldown    = 0.35f;  // seconds between wall jumps on same wall

    // Glide
    float glideGravityScale = 0.15f; // gravity multiplier while gliding
    float glideMinAirTime   = 0.25f; // seconds in air before glide activates
    float glideMaxTime      = 2.50f; // max continuous glide (seconds)

    // Coyote time
    float coyoteTime = 0.12f; // grace window after walking off a ledge

    // Step / clip
    float stepHeight = 18.0f; // auto-step over this many qu
};

// ---------------------------------------------------------------------------
// System entry point — call once per game tick
// ---------------------------------------------------------------------------

void physicsUpdate(entt::registry& reg, const World& world, const PhysicsConfig& cfg, float dt);
