/// @file MovementSystem.cpp
/// @brief Implementation of the Titanfall-inspired movement state machine.

#include "ecs/systems/MovementSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/WallDetection.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace systems
{

// Helper utilities

namespace
{

/// @brief Compute horizontal speed (XZ plane).
/// @param v  Velocity vector.
/// @return Horizontal speed magnitude.
float horizSpeed(const glm::vec3& v)
{
    return std::sqrt(v.x * v.x + v.z * v.z);
}

/// @brief Compute horizontal velocity (Y zeroed).
/// @param v  Velocity vector.
/// @return Velocity with Y component set to zero.
glm::vec3 horizVel(const glm::vec3& v)
{
    return {v.x, 0.0f, v.z};
}

/// @brief Clamp horizontal speed without affecting Y.
/// @param v         Velocity vector to clamp (modified in place).
/// @param maxSpeed  Maximum allowed horizontal speed.
void clampHorizSpeed(glm::vec3& v, float maxSpeed)
{
    const float k_hs = horizSpeed(v);
    if (k_hs > maxSpeed && k_hs > 0.001f) {
        const float k_scale = maxSpeed / k_hs;
        v.x *= k_scale;
        v.z *= k_scale;
    }
}

/// @brief Get WASD input as a 2D vector (X=strafe, Y=forward).
/// @param input  Current input snapshot.
/// @return 2D movement vector.
glm::vec2 moveInput2D(const InputSnapshot& input)
{
    float x = 0.0f;
    float y = 0.0f;
    if (input.forward)
        y += 1.0f;
    if (input.back)
        y -= 1.0f;
    if (input.left)
        x += 1.0f;
    if (input.right)
        x -= 1.0f;
    return {x, y};
}

/// @brief Check whether any WASD movement key is pressed.
/// @param input  Current input snapshot.
/// @return True if any directional key is held.
bool anyMoveInput(const InputSnapshot& input)
{
    return input.forward || input.back || input.left || input.right;
}

} // namespace

// Crouch / shape transition

namespace
{

/// @brief Handle entering the crouch state and adjusting the collision shape.
/// @param pos    Entity position (modified in place).
/// @param shape  Collision shape (modified in place).
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot.
void handleCrouchTransition(Position& pos, CollisionShape& shape, PlayerState& state, const InputSnapshot& input)
{
    const bool k_wantsCrouch = input.crouch;
    const bool k_isCrouched = state.crouching;

    // Enter crouch immediately.
    if (k_wantsCrouch && !k_isCrouched) {
        state.crouching = true;
        shape.halfExtents.y = tms::k_crouchingHalfHeight;
        pos.value.y -= (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);
    }
    // Uncrouch is handled by the auto-uncrouch pass at tick end (step 10)
    // which checks for collision before expanding the AABB.
}

} // namespace

// Timer updates

namespace
{

/// @brief Tick all cooldown and state timers, run every tick regardless of mode.
/// @param state  Player state (modified in place).
/// @param dt     Fixed physics delta time in seconds.
void tickTimers(PlayerState& state, float dt)
{
    state.jumpedThisTick = false;

    // Jump cooldown countdown.
    if (state.jumpCooldown > 0.0f)
        state.jumpCooldown -= dt;

    // Coyote time countdown.
    if (state.coyoteTimer > 0.0f)
        state.coyoteTimer -= dt;

    // Jump lurch timer.
    if (state.jumpLurchEnabled) {
        state.jumpLurchTimer += dt;
        if (state.jumpLurchTimer >= tms::k_jumpLurchGraceMax)
            state.jumpLurchEnabled = false;
    }

    // Grounded-duration accumulator. Used to gate lurch arming on ground jumps:
    // a bhop-chain landing only touches ground for 1-2 ticks, so groundedDuration
    // stays well below k_jumpLurchMinGroundedTime and lurch stays disarmed for that
    // jump. A "fresh" ground jump (player standing for ≥ the threshold) re-arms it.
    if (state.grounded)
        state.groundedDuration += dt;
    else
        state.groundedDuration = 0.0f;

    // Slide boost cooldown.
    if (state.slideBoostCooldown > 0.0f)
        state.slideBoostCooldown -= dt;

    // Slide fatigue recovery (1 level per k_slideFatigueDecayTicks).
    if (state.slideFatigueCounter > 0 && state.moveMode != MoveMode::Sliding) {
        state.slideFatigueDecayAccum++;
        if (state.slideFatigueDecayAccum >= tms::k_slideFatigueDecayTicks) {
            state.slideFatigueDecayAccum = 0;
            state.slideFatigueCounter--;
        }
    }

    // Exit-wall / exit-climb / exit-ledge timers.
    if (state.exitingWall) {
        state.exitWallTimer -= dt;
        if (state.exitWallTimer <= 0.0f) {
            state.exitingWall = false;
            state.wasWallRunning = false;
        }
    }
    if (state.exitingClimb) {
        state.exitClimbTimer -= dt;
        if (state.exitClimbTimer <= 0.0f) {
            state.exitingClimb = false;
            state.wasClimbing = false;
        }
    }
    if (state.exitingLedge) {
        state.exitLedgeTimer -= dt;
        if (state.exitLedgeTimer <= 0.0f)
            state.exitingLedge = false;
    }

    // Grapple cooldown.
    if (state.grappleCooldownActive) {
        state.grappleCooldownTimer -= dt;
        if (state.grappleCooldownTimer <= 0.0f)
            state.grappleCooldownActive = false;
    }
}

} // namespace

// Sprint

namespace
{

/// @brief Update the sprint flag based on input and current state.
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot.
void updateSprint(PlayerState& state, const InputSnapshot& input)
{
    if (input.sprint && input.forward && !state.crouching && state.grounded && state.moveMode == MoveMode::OnFoot) {
        state.sprinting = true;
    } else if (!input.sprint || !input.forward || state.crouching || !state.grounded) {
        state.sprinting = false;
    }
}

} // namespace

// Public: current wish speed (also used by DebugUI).
float currentWishSpeed(const PlayerState& state)
{
    if (state.moveMode == MoveMode::Sliding)
        return 0.0f; // slide has no wish-speed-driven accel
    if (state.crouching)
        return tms::k_crouchSpeed;
    if (state.sprinting)
        return tms::k_sprintSpeed;
    return tms::k_walkSpeed;
}

// Jumping (ground, double, coyote, wall, climb, ledge, slidehop)

namespace
{

/// @brief Handle all jump types: ground, double, coyote, wall, climb, ledge, slidehop.
/// @param vel    Velocity (modified in place).
/// @param input  Current input snapshot.
/// @param state  Player state (modified in place).
/// @param dt     Fixed physics delta time in seconds (unused).
void handleJump(glm::vec3& vel, const InputSnapshot& input, PlayerState& state, float /*dt*/)
{
    if (!input.jump) {
        // Key released — clear the wallrun autobhop lock so the next press
        // registers as intentional and is allowed to wall-jump.
        state.wallJumpLocked = false;
        return;
    }

    // Ledge jump / mantle
    if (state.moveMode == MoveMode::LedgeGrabbing) {
        if (state.ledgeHoldTimer >= tms::k_ledgeMinHoldTime) {
            // Mantle: jump up onto the ledge.
            vel.y = tms::k_ledgeJumpUpForce;
            // Push away from wall (which actually pushes over the ledge since normal points away from wall).
            vel += state.ledgeNormal * tms::k_ledgeJumpBackForce;
            state.moveMode = MoveMode::OnFoot;
            state.exitingLedge = true;
            state.exitLedgeTimer = tms::k_ledgeExitTime;
            state.grounded = false;
            state.jumpCount = 1;
            state.canDoubleJump = true;
        }
        return;
    }

    // Wall jump
    if (state.moveMode == MoveMode::WallRunning) {
        // If jump was held from before wallrun entry, swallow it — a release
        // and re-press is required to wall-jump. Keeps bhop → wallrun flow
        // smooth when autobhop is on.
        if (state.wallJumpLocked)
            return;

        vel.y = tms::k_wallJumpUpForce;
        vel += state.wallNormal * tms::k_wallJumpSideForce;
        state.moveMode = MoveMode::OnFoot;
        state.exitingWall = true;
        state.exitWallTimer = tms::k_wallrunExitTime;
        state.wasWallRunning = true;
        state.grounded = false;
        state.canDoubleJump = true;
        state.jumpCount = 1;

        // Blacklist this wall.
        state.wallBlacklistActive = true;
        state.wallBlacklistNormal = state.wallNormal;
        return;
    }

    // Climb jump
    if (state.moveMode == MoveMode::Climbing) {
        vel.y = tms::k_climbJumpUpForce;
        vel += state.climbWallNormal * tms::k_climbJumpBackForce;
        state.moveMode = MoveMode::OnFoot;
        state.exitingClimb = true;
        state.exitClimbTimer = tms::k_climbExitTime;
        state.wasClimbing = true;
        state.grounded = false;
        state.canDoubleJump = true;
        state.jumpCount = 1;

        state.climbBlacklistActive = true;
        state.climbBlacklistNormal = state.climbWallNormal;
        return;
    }

    // Coyote wall jump (off wall within grace period)
    if (!state.grounded && state.coyoteTimer > 0.0f && state.wasWallRunning) {
        // Same autobhop lock applies — if the player slipped off the wall
        // while jump was held continuously from pre-entry, don't retroactively
        // fire a coyote wall jump until they release and re-press.
        if (state.wallJumpLocked)
            return;

        vel.y = tms::k_wallJumpUpForce;
        vel += state.wallBlacklistNormal * tms::k_wallJumpSideForce;
        state.coyoteTimer = 0.0f;
        state.wasWallRunning = false;
        state.canDoubleJump = true;
        state.jumpCount = 1;
        state.jumpedThisTick = true;
        return;
    }

    // Slidehop
    if (state.moveMode == MoveMode::Sliding) {
        vel.y = tms::k_slidehopJumpSpeed;
        state.moveMode = MoveMode::OnFoot;
        state.crouching = false;
        state.grounded = false;
        state.jumpCount = 1;
        state.canDoubleJump = true;
        state.jumpedThisTick = true;

        // Restore standing shape — the slide had us crouched.
        // Shape/pos update is safe here because we're jumping upward.
        // (Handled via the pendingUncrouch path at tick end to avoid duplication.)
        state.pendingUncrouch = true;

        // Fatigue: increase counter (reduces future slide boosts).
        state.slideFatigueCounter = std::min(state.slideFatigueCounter + 1, tms::k_slideFatigueMax);
        return;
    }

    // Ground jump (or coyote ground jump)
    if (state.grounded || state.coyoteTimer > 0.0f) {
        vel.y = tms::k_jumpSpeed;
        state.grounded = false;
        state.coyoteTimer = 0.0f;
        state.jumpCount = 1;
        state.canDoubleJump = true;
        state.jumpedThisTick = true;
        state.jumpCooldown = tms::k_doubleJumpCooldown;

        // Set up jump lurch — only re-arm for "fresh" ground jumps. Bhop-chain
        // re-jumps have groundedDuration ≈ 1-2 ticks, which stays well below
        // k_jumpLurchMinGroundedTime, so lurch stays disarmed and doesn't fire
        // the 180 u/s sideways redirect + 12.5 % speed haircut on the next
        // strafe change.
        const bool k_freshGroundJump = state.groundedDuration >= tms::k_jumpLurchMinGroundedTime;
        state.jumpLurchEnabled = (tms::k_enableJumpLurch != 0) && k_freshGroundJump;
        state.jumpLurchTimer = 0.0f;
        state.moveInputsOnJump = moveInput2D(input);
        return;
    }

    // Double jump
    // Requires: (a) re-press of jump key (not held from first jump),
    //           (b) cooldown expired since last jump.
    const bool k_jumpRisingEdge = input.jump && !state.jumpHeldLastTick;
    if (state.canDoubleJump && state.jumpCount < 2 && k_jumpRisingEdge && state.jumpCooldown <= 0.0f) {
        // Reset vertical velocity before applying double jump (feels better than additive).
        if (vel.y < 0.0f)
            vel.y = 0.0f;
        vel.y += tms::k_doubleJumpSpeed;
        state.canDoubleJump = false;
        state.jumpCount = 2;
        state.jumpedThisTick = true;

        // Lurch resets on double jump too — but only if the preceding ground jump
        // was itself "fresh". Since double jump happens mid-air, groundedDuration
        // is always 0 here, so this gate makes double-jump NEVER re-arm lurch.
        // That's intentional: bhop+doublejump chains keep lurch disarmed, and
        // lurch remains the deliberate "direction correction" feature reserved
        // for standing-start jumps only.
        const bool k_freshGroundJump = state.groundedDuration >= tms::k_jumpLurchMinGroundedTime;
        state.jumpLurchEnabled = (tms::k_enableJumpLurch != 0) && k_freshGroundJump;
        state.jumpLurchTimer = 0.0f;
        state.moveInputsOnJump = moveInput2D(input);
    }
}

} // namespace

// Jump Lurch

namespace
{

/// @brief Apply directional correction window after jumping.
/// @param vel    Velocity (modified in place).
/// @param input  Current input snapshot.
/// @param state  Player state (modified in place).
void handleJumpLurch(glm::vec3& vel, const InputSnapshot& input, PlayerState& state)
{
    if constexpr (tms::k_enableJumpLurch == 0) {
        // Jump lurch disabled globally: clear state so timers don't accumulate and bail.
        state.jumpLurchEnabled = false;
        state.jumpLurchTimer = 0.0f;
        return;
    }

    if (!state.jumpLurchEnabled)
        return;

    const glm::vec2 k_currentInput = moveInput2D(input);

    // Only trigger lurch if the player is pressing a DIFFERENT direction than when they jumped.
    if (k_currentInput == state.moveInputsOnJump || glm::length(k_currentInput) < 0.01f)
        return;

    // Lurch strength decays linearly from max at graceMin to 0 at graceMax.
    const float k_t = state.jumpLurchTimer;
    float strength = 1.0f;
    if (k_t > tms::k_jumpLurchGraceMin) {
        strength = 1.0f - (k_t - tms::k_jumpLurchGraceMin) / (tms::k_jumpLurchGraceMax - tms::k_jumpLurchGraceMin);
        strength = std::clamp(strength, 0.0f, 1.0f);
    }

    if (strength <= 0.0f)
        return;

    // Compute lurch direction from current WASD + yaw.
    const glm::vec3 k_wishDir = physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
    if (glm::length(k_wishDir) < 0.001f)
        return;

    // Lurch velocity: base * strength_multiplier * decay, clamped.
    float lurchMag = tms::k_jumpLurchBaseVelocity * tms::k_jumpLurchStrength * strength;
    lurchMag = std::min(lurchMag, tms::k_jumpLurchMax);

    // Apply lurch as velocity redirect toward wish direction.
    const glm::vec3 k_lurchVel = k_wishDir * lurchMag;
    vel.x += k_lurchVel.x;
    vel.z += k_lurchVel.z;

    // Speed loss tradeoff.
    const float k_hs = horizSpeed(vel);
    if (k_hs > 0.001f) {
        const float k_newSpeed = k_hs * (1.0f - tms::k_jumpLurchSpeedLoss);
        clampHorizSpeed(vel, k_newSpeed);
    }

    // Disable lurch after application (one-shot per jump).
    state.jumpLurchEnabled = false;
}

} // namespace

// Sliding

namespace
{

/// @brief Try to enter slide: must be moving fast enough and pressing crouch while grounded.
/// @param vel    Velocity (modified in place).
/// @param state  Player state (modified in place).
/// @param shape  Collision shape (modified in place).
/// @param pos    Entity position (modified in place).
/// @param input  Current input snapshot.
void tryEnterSlide(glm::vec3& vel, PlayerState& state, CollisionShape& shape, Position& pos, const InputSnapshot& input)
{
    if (state.moveMode != MoveMode::OnFoot)
        return;
    if (!input.crouch || !state.grounded || !state.canEnterSlide)
        return;

    const float k_hs = horizSpeed(vel);
    if (k_hs < tms::k_slideMinStartSpeed)
        return;

    // Enter slide.
    state.moveMode = MoveMode::Sliding;
    state.slideTimer = 0.0f;
    state.crouching = true;
    shape.halfExtents.y = tms::k_crouchingHalfHeight;
    pos.value.y -= (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);

    // Slide boost (if not on cooldown and fatigue allows).
    if (state.slideBoostCooldown <= 0.0f) {
        const float k_fatigueScale =
            1.0f - static_cast<float>(state.slideFatigueCounter) / static_cast<float>(tms::k_slideFatigueMax);
        const float k_boost = std::lerp(tms::k_slideBoostMin,
                                        tms::k_slideBoostMax,
                                        std::clamp((k_hs - tms::k_slideMinStartSpeed) / 200.0f, 0.0f, 1.0f));
        const float k_actualBoost = k_boost * std::max(0.0f, k_fatigueScale);

        if (k_actualBoost > 0.0f) {
            // Add boost in the current horizontal direction.
            const glm::vec3 k_horizDir = glm::normalize(horizVel(vel));
            vel += k_horizDir * k_actualBoost;
        }
        state.slideBoostCooldown = tms::k_slideBoostCooldown;
    }
}

/// @brief Process sliding movement, braking, and slope influence.
/// @param vel    Velocity (modified in place).
/// @param state  Player state (modified in place).
/// @param shape  Collision shape (modified in place).
/// @param pos    Entity position (modified in place).
/// @param input  Current input snapshot.
/// @param dt     Fixed physics delta time in seconds.
void handleSliding(
    glm::vec3& vel, PlayerState& state, CollisionShape& shape, Position& pos, const InputSnapshot& input, float dt)
{
    state.slideTimer += dt;

    // Exit conditions: release crouch, too slow, or airborne.
    const float k_hs = horizSpeed(vel);
    if (!input.crouch || k_hs < tms::k_slideMinSpeed || !state.grounded) {
        state.moveMode = MoveMode::OnFoot;
        if (!input.crouch) {
            state.crouching = false;
            shape.halfExtents.y = tms::k_standingHalfHeight;
            pos.value.y += (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);
        }
        return;
    }

    // Braking deceleration ramps up over slide duration.
    const float k_brakingAlpha = std::clamp(state.slideTimer / tms::k_slideBrakingRampTime, 0.0f, 1.0f);
    const float k_braking = std::lerp(tms::k_slideBrakingDecelMin, tms::k_slideBrakingDecelMax, k_brakingAlpha);

    // Apply braking in the direction of horizontal motion.
    if (k_hs > 0.001f) {
        const float k_newSpeed = std::max(0.0f, k_hs - k_braking * dt);
        const float k_scale = k_newSpeed / k_hs;
        vel.x *= k_scale;
        vel.z *= k_scale;
    }

    // Surface angle influence: slopes accelerate/decelerate the slide.
    // A perfectly flat floor has groundNormal = (0,1,0), slopeForce = 0.
    // Downhill: the gravity component along the slope adds speed.
    // Uphill: it subtracts speed.
    if (state.groundNormal.y < 0.999f && state.groundNormal.y > 0.01f) {
        // Project gravity onto the slope surface to get the slide force direction.
        const glm::vec3 k_gravity{0.0f, -1.0f, 0.0f};
        const glm::vec3 k_slopeDir =
            glm::normalize(k_gravity - state.groundNormal * glm::dot(k_gravity, state.groundNormal));
        vel += k_slopeDir * tms::k_slideFloorInfluenceForce * dt;
    }
}

} // namespace

// Wallrunning

namespace
{

/// @brief Check if a wall normal matches the blacklist (same wall).
/// @param normal    Wall normal to test.
/// @param height    Current entity height.
/// @param blNormal  Blacklisted wall normal.
/// @param blHeight  Height at which the blacklisted wall was recorded.
/// @param active    Whether the blacklist is active.
/// @return True if the wall is blacklisted.
bool isBlacklisted(const glm::vec3& normal, float height, const glm::vec3& blNormal, float blHeight, bool active)
{
    if (!active)
        return false;
    // Same wall = similar normal. Must be at a lower height to regrab.
    if (glm::dot(normal, blNormal) > 0.9f && height >= blHeight)
        return true;
    return false;
}

/// @brief Attempt to enter wallrun mode when airborne near a wall.
/// @param vel    Velocity (modified in place).
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot.
/// @param walls  Wall detection result from this tick.
/// @param posY   Current vertical position of the entity.
void tryEnterWallrun(glm::vec3& vel,
                     PlayerState& state,
                     const InputSnapshot& input,
                     const physics::WallDetectionResult& walls,
                     float posY)
{
    if (state.moveMode != MoveMode::OnFoot)
        return;
    if (state.grounded || state.exitingWall)
        return;
    if (walls.groundDistance < tms::k_wallrunMinGroundDist)
        return;

    // Directional intent — the player's wish direction must have a component
    // pointing INTO the wall (i.e., along -wallNormal). This replaces the old
    // W-only gate with a rotation-symmetric rule: strafe into the wall, press
    // W toward a wall you're facing, or any combination whose wishDir has a
    // positive projection onto -wallNormal will enter the run.
    const glm::vec3 k_wishDir = physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);

    // Check each side.
    auto tryWall = [&](bool hasWall, const glm::vec3& wallNorm, WallSide side) {
        if (!hasWall)
            return false;
        if (isBlacklisted(
                wallNorm, posY, state.wallBlacklistNormal, state.wallBlacklistHeight, state.wallBlacklistActive))
            return false;
        // Intent check is per-side because it depends on this wall's normal.
        if (glm::dot(k_wishDir, -wallNorm) < tms::k_wallrunIntentThreshold)
            return false;

        // Compute forward direction along wall.
        const glm::vec3 k_up{0, 1, 0};
        glm::vec3 wallFwd = glm::cross(k_up, wallNorm);
        if (side == WallSide::Left)
            wallFwd = -wallFwd;

        // Make sure the player is moving somewhat along the wall (not directly at it).
        const glm::vec3 k_hv = horizVel(vel);
        if (glm::length(k_hv) > 0.001f && glm::dot(glm::normalize(k_hv), wallFwd) < 0.0f)
            wallFwd = -wallFwd; // flip to match movement direction

        state.moveMode = MoveMode::WallRunning;
        state.wallRunSide = side;
        state.wallNormal = wallNorm;
        state.wallForward = wallFwd;
        state.wallRunTimer = 0.0f;
        state.wallRunSpeedTimer = 0.0f;
        state.canDoubleJump = true;
        state.jumpCount = 0;

        // Autobhop lock: if jump was held continuously from before entry (i.e.
        // last tick AND this tick), the player rolled into this wallrun on a
        // bhop chain — swallow the held jump so they don't instantly bounce
        // off. A fresh press on this same tick (jump held now but NOT last
        // tick) counts as genuine intent and fires normally.
        state.wallJumpLocked = input.jump && state.jumpHeldLastTick;

        // Reduce vertical velocity to near-zero for a smooth wall-grab feel.
        vel.y = std::clamp(vel.y, -25.0f, 25.0f);

        return true;
    };

    if (!tryWall(walls.wallRight, walls.rightNormal, WallSide::Right))
        tryWall(walls.wallLeft, walls.leftNormal, WallSide::Left);
}

/// @brief Exit wallrun mode and start cooldown timers.
/// @param state  Player state (modified in place).
/// @param posY   Current vertical position for blacklist height.
void exitWallrun(PlayerState& state, float posY)
{
    state.moveMode = MoveMode::OnFoot;
    state.exitingWall = true;
    state.exitWallTimer = tms::k_wallrunExitTime;
    state.wasWallRunning = true;
    state.coyoteTimer = tms::k_coyoteTime;
    state.wallBlacklistActive = true;
    state.wallBlacklistNormal = state.wallNormal;
    state.wallBlacklistHeight = posY;
}

/// @brief Process wallrunning movement, exit conditions, and camera tilt.
/// @param pos          Entity position (modified for curved-surface correction).
/// @param vel          Velocity (modified in place).
/// @param state        Player state (modified in place).
/// @param input        Current input snapshot.
/// @param walls        Wall detection result from this tick.
/// @param halfExtents  Player AABB half-extents (for standoff distance).
/// @param posY         Current vertical position of the entity.
/// @param dt           Fixed physics delta time in seconds.
void handleWallRunning(glm::vec3& pos,
                       glm::vec3& vel,
                       PlayerState& state,
                       const InputSnapshot& input,
                       const physics::WallDetectionResult& walls,
                       const glm::vec3& halfExtents,
                       float posY,
                       float dt)
{
    state.wallRunTimer += dt;
    state.wallRunSpeedTimer += dt;

    // Exit: max duration
    if (state.wallRunTimer >= tms::k_wallrunKickoffDuration) {
        exitWallrun(state, posY);
        return;
    }

    // Exit: lost wall contact
    const bool k_stillOnWall = (state.wallRunSide == WallSide::Right && walls.wallRight) ||
                               (state.wallRunSide == WallSide::Left && walls.wallLeft);
    if (!k_stillOnWall) {
        exitWallrun(state, posY);
        return;
    }

    // Detach intent: the player can look up to 15° away from the wall plane
    // before detaching.  This makes jump-off more intuitive — start turning
    // away, then jump, and you still get the wall-jump boost.
    const glm::vec3 k_wishDir = physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
    const float k_wishLen = glm::length(k_wishDir);
    if (k_wishLen > 0.001f) {
        const float k_intentDot = glm::dot(k_wishDir / k_wishLen, -state.wallNormal);
        if (k_intentDot < tms::k_wallrunDetachThreshold) {
            exitWallrun(state, posY);
            return;
        }
    }

    // --- Update wall normal from latest detection (curved surface tracking) ---
    if (state.wallRunSide == WallSide::Right)
        state.wallNormal = walls.rightNormal;
    else
        state.wallNormal = walls.leftNormal;

    // --- Curved surface: constrained motion ---
    // On a flat wall, tangential velocity is parallel to the surface.
    // On a curved surface (cylinder), the tangent rotates, so velocity must
    // be re-projected onto the new tangent plane each tick.  Without this,
    // the player flies off tangentially — the push force (300 u/s²) can't
    // overcome the centripetal acceleration needed (v²/r ≈ 2000+ u/s²).

    // 1. Preserve speed, re-project velocity onto the new wall tangent plane.
    //    This is the key step — it rotates the velocity to follow curvature.
    {
        const float k_normalVel = glm::dot(vel, state.wallNormal);
        // Remove the outward component (only if pointing away from wall).
        // Keep inward component so the push force can press the player in.
        if (k_normalVel > 0.0f)
            vel -= state.wallNormal * k_normalVel;
    }

    // 2. Position correction: if the wall contact point is known, nudge the
    //    player toward the wall to maintain consistent standoff distance.
    {
        const glm::vec3 wallPt = (state.wallRunSide == WallSide::Right) ? walls.rightPoint : walls.leftPoint;
        // Vector from wall contact point to player center, along wall normal.
        const float k_currentDist = glm::dot(pos - wallPt, state.wallNormal);
        // Desired standoff: just outside the collision shape.
        const float k_desiredDist = std::max(halfExtents.x, halfExtents.z) + 1.0f;
        const float k_drift = k_currentDist - k_desiredDist;
        // If drifting outward, pull back. Lerp to avoid jitter.
        if (k_drift > 0.5f) {
            const float k_correction = std::min(k_drift * 10.0f * dt, k_drift);
            pos -= state.wallNormal * k_correction;
        }
    }

    // --- Compute wall-tangent acceleration direction ---
    // Accelerate along the current velocity direction projected onto the wall
    // plane.  This makes the controls intuitive: as long as wishDir has a
    // component into the wall, the player is accelerated forward regardless
    // of strafe keys.
    const glm::vec3 k_hv = horizVel(vel);
    const float k_hvLen = glm::length(k_hv);

    glm::vec3 wallFwd;
    if (k_hvLen > 1.0f) {
        // Project current velocity onto the wall plane (remove normal component).
        wallFwd = k_hv - state.wallNormal * glm::dot(k_hv, state.wallNormal);
        const float k_projLen = glm::length(wallFwd);
        if (k_projLen > 0.001f)
            wallFwd /= k_projLen;
        else
            wallFwd = state.wallForward;
    } else {
        wallFwd = state.wallForward;
    }

    // Ensure consistency with stored direction (don't flip 180°).
    if (glm::dot(state.wallForward, wallFwd) < 0.0f)
        wallFwd = -wallFwd;
    state.wallForward = wallFwd;

    // Accelerate along the wall.
    const float k_currentFwdSpeed = glm::dot(k_hv, state.wallForward);
    if (k_currentFwdSpeed < tms::k_wallrunMaxSpeed) {
        const float k_addSpeed = std::min(tms::k_wallrunAccel * dt, tms::k_wallrunMaxSpeed - k_currentFwdSpeed);
        vel += state.wallForward * k_addSpeed;
    }

    // Speed clamping (after initial delay to allow wallkick tech).
    if (state.wallRunSpeedTimer > tms::k_wallrunSpeedLossDelay)
        clampHorizSpeed(vel, tms::k_wallrunMaxSpeed);

    // Push toward wall to keep player stuck.
    vel -= state.wallNormal * tms::k_wallrunPushForce * dt;

    // Gradual slide-off: during the grip window, pin vertical velocity to 0 so
    // the wallrun feels "stuck." Afterwards, gravity ramps in linearly over
    // `k_wallrunGravityRampTime`; by the end of the ramp the player is falling
    // at full gravity, which naturally slides them down the wall and prevents
    // indefinite runs even if the player stays within the kickoff timer.
    if (state.wallRunTimer < tms::k_wallrunGripTime) {
        vel.y = 0.0f;
    } else {
        const float k_rampT = (state.wallRunTimer - tms::k_wallrunGripTime) / tms::k_wallrunGravityRampTime;
        const float k_gravFactor = std::clamp(k_rampT, 0.0f, 1.0f);
        vel.y -= physics::k_gravity * k_gravFactor * dt;
    }

    // Camera tilt.
    state.targetCameraTilt =
        (state.wallRunSide == WallSide::Right) ? tms::k_wallrunCameraTilt : -tms::k_wallrunCameraTilt;
}

} // namespace

// Climbing

namespace
{

/// @brief Attempt to enter climb mode when airborne facing a wall.
/// @param vel    Velocity (modified in place).
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot.
/// @param walls  Wall detection result from this tick.
/// @param posY   Current vertical position of the entity.
void tryEnterClimb(glm::vec3& vel,
                   PlayerState& state,
                   const InputSnapshot& input,
                   const physics::WallDetectionResult& walls,
                   float posY)
{
    if (state.moveMode != MoveMode::OnFoot)
        return;
    if (state.grounded || state.exitingClimb)
        return;
    if (!input.forward)
        return;
    if (walls.groundDistance < tms::k_climbMinGroundDist)
        return;
    if (!walls.wallFront)
        return;

    // Check look angle: player must be facing the wall.
    const float k_sinYaw = std::sin(input.yaw);
    const float k_cosYaw = std::cos(input.yaw);
    const glm::vec3 k_lookDir{k_sinYaw, 0.0f, k_cosYaw};
    const float k_lookAngle = std::acos(std::clamp(glm::dot(-k_lookDir, walls.frontNormal), -1.0f, 1.0f));
    const float k_maxAngleRad = glm::radians(tms::k_climbMaxWallLookAngle);
    if (k_lookAngle > k_maxAngleRad)
        return;

    // Blacklist check.
    if (isBlacklisted(walls.frontNormal,
                      posY,
                      state.climbBlacklistNormal,
                      state.climbBlacklistHeight,
                      state.climbBlacklistActive))
        return;

    // Enter climbing.
    state.moveMode = MoveMode::Climbing;
    state.climbWallNormal = walls.frontNormal;
    state.climbTimer = 0.0f;
    state.canDoubleJump = true;
    state.jumpCount = 0;

    // Reduce horizontal velocity immediately.
    vel.x *= tms::k_climbSidewaysMultiplier;
    vel.z *= tms::k_climbSidewaysMultiplier;
    vel.y = std::max(vel.y, 0.0f);
}

/// @brief Exit climb mode and start cooldown timers.
/// @param state  Player state (modified in place).
/// @param posY   Current vertical position for blacklist height.
void exitClimb(PlayerState& state, float posY)
{
    state.moveMode = MoveMode::OnFoot;
    state.exitingClimb = true;
    state.exitClimbTimer = tms::k_climbExitTime;
    state.wasClimbing = true;
    state.coyoteTimer = tms::k_coyoteTime;
    state.climbBlacklistActive = true;
    state.climbBlacklistNormal = state.climbWallNormal;
    state.climbBlacklistHeight = posY;
}

/// @brief Process climbing movement with speed decay and exit conditions.
/// @param vel    Velocity (modified in place).
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot.
/// @param walls  Wall detection result from this tick.
/// @param posY   Current vertical position of the entity.
/// @param dt     Fixed physics delta time in seconds.
void handleClimbing(glm::vec3& vel,
                    PlayerState& state,
                    const InputSnapshot& input,
                    const physics::WallDetectionResult& walls,
                    float posY,
                    float dt)
{
    state.climbTimer += dt;

    // Exit conditions
    if (state.climbTimer >= tms::k_climbKickoffDuration || !walls.wallFront || !input.forward) {
        exitClimb(state, posY);
        return;
    }

    // Climbing movement (upward with speed decay)
    const float k_decayAlpha = std::clamp(state.climbTimer / tms::k_climbKickoffDuration, 0.0f, 1.0f);
    const float k_climbSpeed = std::lerp(tms::k_climbMaxSpeed, tms::k_climbMinSpeed, k_decayAlpha);

    vel.y = k_climbSpeed;

    // Minimal sideways movement.
    vel.x *= tms::k_climbSidewaysMultiplier;
    vel.z *= tms::k_climbSidewaysMultiplier;

    // Push toward wall.
    vel -= state.climbWallNormal * tms::k_wallrunPushForce * dt;

    state.targetCameraTilt = 0.0f;
}

} // namespace

// Ledge grabbing

namespace
{

/// @brief Attempt to grab a ledge while climbing.
/// @param state  Player state (modified in place).
/// @param walls  Wall detection result from this tick.
void tryEnterLedgeGrab(PlayerState& state, const physics::WallDetectionResult& walls)
{
    // Can only grab ledges while climbing.
    if (state.moveMode != MoveMode::Climbing)
        return;
    if (!walls.ledgeDetected)
        return;
    if (state.exitingLedge)
        return;

    state.moveMode = MoveMode::LedgeGrabbing;
    state.ledgePoint = walls.ledgePoint;
    state.ledgeNormal = walls.ledgeNormal;
    state.ledgeHoldTimer = 0.0f;
    state.canDoubleJump = true;
    state.jumpCount = 0;
}

/// @brief Process ledge grab hold, freeze velocity, and auto-mantle.
/// @param vel    Velocity (modified in place).
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot.
/// @param dt     Fixed physics delta time in seconds.
void handleLedgeGrab(glm::vec3& vel, PlayerState& state, const InputSnapshot& input, float dt)
{
    state.ledgeHoldTimer += dt;

    // Freeze velocity (gravity is countered).
    vel = glm::vec3(0.0f);

    // Auto-mantle: if holding movement keys past min hold time.
    if (state.ledgeHoldTimer >= tms::k_ledgeMinHoldTime && anyMoveInput(input)) {
        vel.y = tms::k_ledgeJumpUpForce;
        vel += state.ledgeNormal * tms::k_ledgeJumpBackForce;
        state.moveMode = MoveMode::OnFoot;
        state.exitingLedge = true;
        state.exitLedgeTimer = tms::k_ledgeExitTime;
        state.canDoubleJump = true;
        state.jumpCount = 1;
    }

    state.targetCameraTilt = 0.0f;
}

} // namespace

// Coyote time

namespace
{

/// @brief Start coyote timer when transitioning from grounded to airborne.
/// @param state  Player state (modified in place).
void updateCoyoteTime(PlayerState& state)
{
    // Start coyote timer when transitioning from grounded to airborne.
    if (state.wasGroundedLastTick && !state.grounded && state.moveMode == MoveMode::OnFoot && !state.jumpedThisTick) {
        state.coyoteTimer = tms::k_coyoteTime;
    }

    state.wasGroundedLastTick = state.grounded;
}

} // namespace

// Grappling hook — Widowmaker-style (direct pull → look-biased launch)
//
// Physics model:
//   FIRE:   Press E → raycast forward → attach to first surface hit.
//   PULL:   Velocity is overridden each tick to point directly at the anchor.
//           No air control, no gravity, no steering. Pure linear pull.
//   DETACH: On arrival (close to anchor), jump cancel, or safety timeout.
//           Velocity is redirected: blend grapple direction with look direction.
//           Looking up converts horizontal pull speed into a vertical arc.
//           This is the signature "look-up launch" that creates skill expression.

namespace
{

/// @brief Compute the player's 3D look direction from yaw/pitch.
glm::vec3 lookDirFromInput(const InputSnapshot& input)
{
    const float k_cosPitch = std::cos(input.pitch);
    return {std::sin(input.yaw) * k_cosPitch, -std::sin(input.pitch), std::cos(input.yaw) * k_cosPitch};
}

/// @brief Detach the grapple and apply the look-biased launch impulse.
///
/// The launch velocity is a blend of the grapple-line direction and the
/// player's current look direction.  Looking upward near detach converts
/// horizontal pull speed into a soaring vertical arc — this is the core
/// Widowmaker tech that makes the grapple expressive.
void grappleDetachWithLaunch(glm::vec3& vel, PlayerState& state, const InputSnapshot& input, glm::vec3 pos)
{
    const float k_speed = glm::length(vel);

    // Current grapple-line direction (from player toward anchor).
    const glm::vec3 k_toHook = state.grapplePoint - pos;
    const float k_dist = glm::length(k_toHook);
    const glm::vec3 k_grappleDir = (k_dist > 0.001f) ? k_toHook / k_dist : glm::vec3(0, 1, 0);

    // Player look direction (where they're aiming at detach time).
    const glm::vec3 k_lookDir = lookDirFromInput(input);

    // Blend grapple direction with look direction.
    // k_grappleLaunchLookBias = 0.6 → 60% look, 40% grapple line.
    // Looking straight up near detach = massive vertical launch.
    const glm::vec3 k_launchDir =
        glm::normalize(k_grappleDir * (1.0f - tms::k_grappleLaunchLookBias) + k_lookDir * tms::k_grappleLaunchLookBias);

    // Apply launch: preserve momentum with a slight speed boost.
    const float k_launchSpeed = std::max(k_speed, tms::k_grapplePullSpeed * 0.5f) * tms::k_grappleLaunchSpeedMult;
    vel = k_launchDir * k_launchSpeed;

    // End grapple, start cooldown.
    state.grappleActive = false;
    state.grappleCooldownActive = true;
    state.grappleCooldownTimer = tms::k_grappleCooldown;
    state.grounded = false;
}

/// @brief Fire grapple on E press.  One-shot: press E to fire, not hold.
void tryFireGrapple(PlayerState& state, const InputSnapshot& input, glm::vec3 eye, const physics::WorldGeometry& world)
{
    const bool k_pressed = input.grapple && !state.grappleInputLastTick;
    if (!k_pressed || state.grappleActive || state.grappleCooldownActive)
        return;

    // Raycast forward from the eye position.
    const glm::vec3 k_fwd = lookDirFromInput(input);
    const glm::vec3 k_end = eye + k_fwd * tms::k_grappleMaxRange;
    const physics::SphereHitResult k_hit = physics::sphereCast(4.0f, eye, k_end, world);

    if (!k_hit.hit)
        return;

    // Hook attached — begin pull.
    state.grappleActive = true;
    state.grapplePullTimer = 0.0f;
    state.grapplePoint = k_hit.point;
    state.grapplePullDir = glm::normalize(k_hit.point - eye);

    // Force airborne — the grapple lifts you off the ground immediately.
    state.grounded = false;
}

/// @brief Pull the player directly toward the anchor.  No air control, no gravity.
///
/// Velocity is overridden each tick (not additive). The player flies in a
/// straight line toward the anchor point at k_grapplePullSpeed.
void handleGrapple(glm::vec3& vel, PlayerState& state, const InputSnapshot& input, glm::vec3 pos, float /*dt*/)
{
    if (!state.grappleActive)
        return;

    const glm::vec3 k_toHook = state.grapplePoint - pos;
    const float k_dist = glm::length(k_toHook);

    // ── Detach conditions ───────────────────────────────────────────────

    // Arrived at anchor — auto-detach with launch.
    if (k_dist < tms::k_grappleDetachDist) {
        grappleDetachWithLaunch(vel, state, input, pos);
        return;
    }

    // Jump to cancel early — detach with launch (the skill expression).
    if (input.jump) {
        grappleDetachWithLaunch(vel, state, input, pos);
        return;
    }

    // Safety timeout.
    if (state.grapplePullTimer > tms::k_grappleMaxDuration) {
        grappleDetachWithLaunch(vel, state, input, pos);
        return;
    }

    // Crouch to cancel without launch (just drop).
    if (input.crouch) {
        state.grappleActive = false;
        state.grappleCooldownActive = true;
        state.grappleCooldownTimer = tms::k_grappleCooldown;
        return;
    }

    // ── Pull phase: override velocity directly toward anchor ────────────
    // No gradual acceleration. No air control. No gravity.
    // Pure linear flight toward the hook point.
    const glm::vec3 k_pullDir = k_toHook / k_dist;
    vel = k_pullDir * tms::k_grapplePullSpeed;
}

} // namespace

// Speed cap

namespace
{

/// @brief Clamp horizontal speed to the global or grapple speed cap.
/// @param vel    Velocity (modified in place).
/// @param state  Player state.
void applySpeedCap(glm::vec3& vel, const PlayerState& state)
{
    // During grapple pull, velocity is directly controlled — don't cap it.
    if (state.grappleActive)
        return;
    clampHorizSpeed(vel, tms::k_speedCap);
}

} // namespace

// Main entry point

void runMovement(Registry& registry, float dt, const physics::WorldGeometry& world)
{
    // Entities WITH InputSnapshot — full player movement
    registry.view<Position, Velocity, PlayerState, CollisionShape, InputSnapshot>().each(
        [dt,
         &world](Position& pos, Velocity& vel, PlayerState& state, CollisionShape& shape, const InputSnapshot& input) {
            // 0. Tick timers
            tickTimers(state, dt);

            // 1. Wall / climb / ledge detection
            // Pass the previous wall normal so the detector can trace toward
            // curved surfaces (cylinders, concave walls) whose normal rotates
            // as the player moves along them.
            physics::WallDetectionResult walls{};
            if (!state.grounded || state.moveMode == MoveMode::WallRunning || state.moveMode == MoveMode::Climbing) {
                const glm::vec3 prevNormal =
                    (state.moveMode == MoveMode::WallRunning) ? state.wallNormal : glm::vec3(0.0f);
                walls = physics::detectWalls(pos.value,
                                             input.yaw,
                                             shape.halfExtents,
                                             world,
                                             tms::k_wallrunCheckDist,
                                             tms::k_wallrunSphereRadius,
                                             prevNormal);
            }

            // 2. Sprint update
            updateSprint(state, input);

            // 3. State transitions (try enter new modes)
            // Order matters: ledge > climb > wallrun > slide
            tryEnterLedgeGrab(state, walls);
            if (state.moveMode == MoveMode::OnFoot)
                tryEnterClimb(vel.value, state, input, walls, pos.value.y);
            if (state.moveMode == MoveMode::OnFoot)
                tryEnterWallrun(vel.value, state, input, walls, pos.value.y);
            if (state.moveMode == MoveMode::OnFoot)
                tryEnterSlide(vel.value, state, shape, pos, input);

            // 4. Crouch transition (only in OnFoot)
            if (state.moveMode == MoveMode::OnFoot)
                handleCrouchTransition(pos, shape, state, input);

            // 4b. Jump handling — runs BEFORE mode-specific movement.
            //     This matches the canonical Quake/Source pmove order:
            //     PM_CheckJump runs before PM_Friction, so a successful ground
            //     jump sets grounded=false and the ground friction/accel branch
            //     below is skipped on landing ticks. This is what preserves
            //     horizontal momentum across bhop chains — without this, each
            //     landing tick bleeds ~3 % of horizontal speed to ground friction.
            //
            //     Skipped during grapple pull (grapple handles its own
            //     jump-detach internally). `state.grappleActive` here reflects
            //     the previous tick's grapple state, since tryFireGrapple runs
            //     after mode-specific movement below — acceptable because
            //     grapple-rising-edge + jump same-tick is a rare edge case and
            //     handleGrapple's velocity override wins anyway.
            if (!state.grappleActive)
                handleJump(vel.value, input, state, dt);

            // 5. Mode-specific movement
            switch (state.moveMode) {
            case MoveMode::OnFoot: {
                const glm::vec3 k_wishDir =
                    physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
                const float k_wishSpeed = currentWishSpeed(state);

                if (state.grounded) {
                    vel.value = physics::applyGroundFriction(vel.value, dt);
                    if (glm::length(k_wishDir) > 0.001f)
                        vel.value = physics::accelerate(vel.value, k_wishDir, k_wishSpeed, physics::k_groundAccel, dt);
                } else {
                    vel.value = physics::applyGravity(vel.value, dt);
                    if (glm::length(k_wishDir) > 0.001f)
                        vel.value =
                            physics::accelerate(vel.value, k_wishDir, physics::k_airMaxSpeed, physics::k_airAccel, dt);
                }

                // Camera tilt returns to zero.
                state.targetCameraTilt = 0.0f;
                break;
            }

            case MoveMode::Sliding: {
                handleSliding(vel.value, state, shape, pos, input, dt);
                // Slide camera lean: tilt based on lateral velocity relative to look direction.
                const float k_sinY = std::sin(input.yaw);
                const float k_cosY = std::cos(input.yaw);
                const glm::vec3 k_lookRight{k_cosY, 0.0f, -k_sinY};
                const float k_lateralSpeed = glm::dot(horizVel(vel.value), k_lookRight);
                state.targetCameraTilt = std::clamp(k_lateralSpeed / 400.0f * 3.0f, -5.0f, 5.0f);
                break;
            }

            case MoveMode::WallRunning:
                handleWallRunning(pos.value, vel.value, state, input, walls, shape.halfExtents, pos.value.y, dt);
                break;

            case MoveMode::Climbing:
                handleClimbing(vel.value, state, input, walls, pos.value.y, dt);
                break;

            case MoveMode::LedgeGrabbing:
                handleLedgeGrab(vel.value, state, input, dt);
                break;
            }

            // 5b. Grappling hook (Widowmaker-style)
            // Check for fire (rising edge of E). If active, the pull phase
            // overrides ALL movement — no gravity, no air control, no steering.
            // Jump detaches with a look-biased launch. Crouch cancels with drop.
            bool grapplePulling = false;
            {
                const glm::vec3 k_eye = pos.value + glm::vec3(0, shape.halfExtents.y * 0.77f, 0);
                tryFireGrapple(state, input, k_eye, world);
                state.grappleInputLastTick = input.grapple;

                if (state.grappleActive) {
                    // Cancel wallrun/climb/slide on grapple.
                    if (state.moveMode != MoveMode::OnFoot) {
                        state.moveMode = MoveMode::OnFoot;
                        if (state.crouching)
                            state.pendingUncrouch = true;
                    }

                    // Pull overrides velocity. Jump/crouch detach is handled inside.
                    handleGrapple(vel.value, state, input, pos.value, dt);

                    // If still active after handleGrapple, mark as pulling
                    // to skip all remaining movement this tick.
                    grapplePulling = state.grappleActive;
                }
            }

            // 6. Jump handling is now step 4b (before mode-specific movement).
            //    See the comment there for the bhop-preservation rationale.

            // 7. Jump lurch (air only, NOT during grapple)
            if (!grapplePulling && !state.grounded && state.moveMode == MoveMode::OnFoot)
                handleJumpLurch(vel.value, input, state);

            // 8. Coyote time update
            if (!grapplePulling)
                updateCoyoteTime(state);

            // 9. Landing reset
            if (state.grounded && state.moveMode == MoveMode::OnFoot) {
                state.jumpCount = 0;
                state.canDoubleJump = true;
                state.canEnterSlide = true;
                state.jumpLurchEnabled = false;

                // Clear blacklists on landing.
                state.wallBlacklistActive = false;
                state.climbBlacklistActive = false;
            }

            // 10. Auto-uncrouch / pending uncrouch
            // If the player should uncrouch (slidehop exit, or crouch key
            // released while crouched) but collision might block it, we
            // try here. The collision system's depenetration will push us
            // back down if expanding the AABB puts us inside geometry.
            if (state.pendingUncrouch || (state.crouching && !input.crouch && state.moveMode == MoveMode::OnFoot)) {
                // Try to stand up: expand AABB and raise centre.
                state.crouching = false;
                shape.halfExtents.y = tms::k_standingHalfHeight;
                pos.value.y += (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);

                // Check if standing up puts us inside geometry.
                // Use a quick sweep upward from current pos — if it hits
                // immediately, we can't stand and must stay crouched.
                const glm::vec3 k_testEnd = pos.value + glm::vec3(0, 0.1f, 0);
                const physics::HitResult k_test = physics::sweepAll(shape.halfExtents, pos.value, k_testEnd, world);
                if (k_test.hit && k_test.tFirst < 0.01f) {
                    // Can't stand — revert to crouched.
                    state.crouching = true;
                    shape.halfExtents.y = tms::k_crouchingHalfHeight;
                    pos.value.y -= (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);
                }
                state.pendingUncrouch = false;
            }

            // 11. Speed cap
            applySpeedCap(vel.value, state);

            // 12. Track jump key state for edge detection
            state.jumpHeldLastTick = input.jump;
        });

    // projectile entities
    // registry.view<Velocity, Projectile>(entt::exclude<InputSnapshot>)
    //     .each([dt,
    //      &world](Position& pos, Velocity& vel, Projectile& projectile, CollisionShape& shape) {
    //          // vel.value = physics::accelerate(vel.value, k_wishDir, k_wishSpeed, physics::k_groundAccel, dt);
    //          WeaponConfig& config = getWeaponConfig(projectile.type);
    //          vel.value = ;
    //
    //
    //     });
}

} // namespace systems
