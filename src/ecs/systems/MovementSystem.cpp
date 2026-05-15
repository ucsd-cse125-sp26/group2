/// @file MovementSystem.cpp
/// @brief Implementation of the Titanfall-inspired movement state machine.

#include "ecs/systems/MovementSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerSimState.hpp" // also pulls in PlayerVisState + PlayerStateRef
#include "ecs/components/Position.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/physics/Forces.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/WallDetection.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

// PR-7 (server-perf): optional parallel-STL hooks for the player loop.
#if __has_include("perf/Parallel.hpp")
#include "perf/Parallel.hpp"
#define GROUP2_MOVEMENT_HAS_PARALLEL 1
#else
#define GROUP2_MOVEMENT_HAS_PARALLEL 0
#endif

#include <vector>

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
void handleCrouchTransition(Position& pos, CollisionShape& shape, PlayerStateRef state, const InputSnapshot& input)
{
    const bool k_wantsCrouch = input.crouch;
    const bool k_isCrouched = state.vis.crouching;

    // Enter crouch immediately.
    if (k_wantsCrouch && !k_isCrouched) {
        state.vis.crouching = true;
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
void tickTimers(PlayerStateRef state, float dt)
{
    state.sim.jumpedThisTick = false;

    // Jump cooldown countdown.
    if (state.sim.jumpCooldown > 0.0f)
        state.sim.jumpCooldown -= dt;

    // Coyote time countdown.
    if (state.sim.coyoteTimer > 0.0f)
        state.sim.coyoteTimer -= dt;

    // Jump lurch timer.
    if (state.sim.jumpLurchEnabled) {
        state.sim.jumpLurchTimer += dt;
        if (state.sim.jumpLurchTimer >= tms::k_jumpLurchGraceMax)
            state.sim.jumpLurchEnabled = false;
    }

    // Grounded-duration accumulator. Used to gate lurch arming on ground jumps:
    // a bhop-chain landing only touches ground for 1-2 ticks, so groundedDuration
    // stays well below k_jumpLurchMinGroundedTime and lurch stays disarmed for that
    // jump. A "fresh" ground jump (player standing for ≥ the threshold) re-arms it.
    if (state.vis.grounded)
        state.sim.groundedDuration += dt;
    else
        state.sim.groundedDuration = 0.0f;

    // Slide boost cooldown.
    if (state.sim.slideBoostCooldown > 0.0f)
        state.sim.slideBoostCooldown -= dt;

    // Slide fatigue recovery (1 level per k_slideFatigueDecayTicks).
    if (state.sim.slideFatigueCounter > 0 && state.vis.moveMode != MoveMode::Sliding) {
        state.sim.slideFatigueDecayAccum++;
        if (state.sim.slideFatigueDecayAccum >= tms::k_slideFatigueDecayTicks) {
            state.sim.slideFatigueDecayAccum = 0;
            state.sim.slideFatigueCounter--;
        }
    }

    // Exit-wall / exit-climb / exit-ledge timers.
    if (state.vis.exitingWall) {
        state.sim.exitWallTimer -= dt;
        if (state.sim.exitWallTimer <= 0.0f) {
            state.vis.exitingWall = false;
            state.sim.wasWallRunning = false;
        }
    }
    if (state.vis.exitingClimb) {
        state.sim.exitClimbTimer -= dt;
        if (state.sim.exitClimbTimer <= 0.0f) {
            state.vis.exitingClimb = false;
            state.sim.wasClimbing = false;
        }
    }
    if (state.sim.exitingLedge) {
        state.sim.exitLedgeTimer -= dt;
        if (state.sim.exitLedgeTimer <= 0.0f)
            state.sim.exitingLedge = false;
    }

    // Grapple cooldown.
    if (state.sim.grappleCooldownActive) {
        state.sim.grappleCooldownTimer -= dt;
        if (state.sim.grappleCooldownTimer <= 0.0f)
            state.sim.grappleCooldownActive = false;
    }

    // Gravity flip cooldown.
    if (state.sim.gravityFlipCooldown > 0.0f)
        state.sim.gravityFlipCooldown -= dt;
}

} // namespace

// Sprint

namespace
{

/// @brief Update the sprint flag based on input and current state.
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot (sprint input is now ignored).
///
/// Sprint has been removed from the game — base movement speed (tms::k_walkSpeed)
/// is now high enough that a separate sprint state is unnecessary. This function
/// is preserved so the sprinting flag in PlayerVisState (read by animation,
/// networking, and debug UI) stays in a defined state. It is unconditionally
/// false now.
void updateSprint(PlayerStateRef state, const InputSnapshot& /*input*/)
{
    state.vis.sprinting = false;
}

} // namespace

// Public: current wish speed (also used by DebugUI).
float currentWishSpeed(const PlayerVisState& vis)
{
    if (vis.moveMode == MoveMode::Sliding)
        return 0.0f; // slide has no wish-speed-driven accel
    if (vis.crouching)
        return tms::k_crouchSpeed;
    if (vis.sprinting)
        return tms::k_sprintSpeed;
    return tms::k_walkSpeed;
}

// Jumping (ground, double, coyote, wall, climb, ledge, slidehop)

namespace
{

/// @brief Handle all jump types: ground, double, coyote, wall, climb, ledge, slidehop.
/// @param vel     Velocity (modified in place).
/// @param input   Current input snapshot.
/// @param state   Player state (modified in place).
/// @param dt      Fixed physics delta time in seconds (unused).
/// @param gravDir +1.0 for normal gravity, -1.0 for flipped (inverts all vertical impulses).
void handleJump(glm::vec3& vel, const InputSnapshot& input, PlayerStateRef state, float /*dt*/, float gravDir = 1.0f)
{
    if (!input.jump)
        return;

    // Ledge jump / mantle
    if (state.vis.moveMode == MoveMode::LedgeGrabbing) {
        if (state.sim.ledgeHoldTimer >= tms::k_ledgeMinHoldTime) {
            // Mantle: jump up onto the ledge.
            vel.y = tms::k_ledgeJumpUpForce * gravDir;
            // Push away from wall (which actually pushes over the ledge since normal points away from wall).
            vel += state.sim.ledgeNormal * tms::k_ledgeJumpBackForce;
            state.vis.moveMode = MoveMode::OnFoot;
            state.sim.exitingLedge = true;
            state.sim.exitLedgeTimer = tms::k_ledgeExitTime;
            state.vis.grounded = false;
            state.vis.jumpCount = 1;
            // DJ no longer refreshes from ledge mantle — only from ground time.
        }
        return;
    }

    // Wall-jump is no longer fired on jump PRESS while attached. Lucio-style:
    // releasing jump while wallrunning fires the impulse (see handleWallRunning).
    // We don't fall through to other branches because being in WallRunning means
    // no ground/double-jump should fire either.
    if (state.vis.moveMode == MoveMode::WallRunning)
        return;

    // Climb jump
    if (state.vis.moveMode == MoveMode::Climbing) {
        vel.y = tms::k_climbJumpUpForce * gravDir;
        vel += state.sim.climbWallNormal * tms::k_climbJumpBackForce;
        state.vis.moveMode = MoveMode::OnFoot;
        state.vis.exitingClimb = true;
        state.sim.exitClimbTimer = tms::k_climbExitTime;
        state.sim.wasClimbing = true;
        state.vis.grounded = false;
        // DJ no longer refreshes from climb jump — only from ground time.
        state.vis.jumpCount = 1;

        state.sim.climbBlacklistActive = true;
        state.sim.climbBlacklistNormal = state.sim.climbWallNormal;
        return;
    }

    // Coyote wall jump (off wall within grace period). Requires a FRESH press
    // — otherwise, the moment the wallrun silently exits (e.g. wall ends while
    // jump is still held), this branch would fire on the very next tick and
    // look like a phantom auto-jump. Rising-edge gate prevents that.
    const bool k_coyoteRisingEdge = input.jump && !state.sim.jumpHeldLastTick;
    if (k_coyoteRisingEdge && !state.vis.grounded && state.sim.coyoteTimer > 0.0f && state.sim.wasWallRunning) {
        vel.y = tms::k_wallJumpUpForce * gravDir;
        vel += state.sim.wallBlacklistNormal * tms::k_wallJumpSideForce;
        state.sim.coyoteTimer = 0.0f;
        state.sim.wasWallRunning = false;
        // DJ no longer refreshes from coyote wall jump — only from ground time.
        state.vis.jumpCount = 1;
        state.sim.jumpedThisTick = true;
        return;
    }

    // Slidehop
    if (state.vis.moveMode == MoveMode::Sliding) {
        vel.y = tms::k_slidehopJumpSpeed * gravDir;
        state.vis.moveMode = MoveMode::OnFoot;
        state.vis.crouching = false;
        state.vis.grounded = false;
        state.vis.jumpCount = 1;
        // DJ no longer refreshes from slidehop — only from ground time.
        state.sim.jumpedThisTick = true;

        // Restore standing shape — the slide had us crouched.
        // Shape/pos update is safe here because we're jumping upward.
        // (Handled via the pendingUncrouch path at tick end to avoid duplication.)
        state.vis.pendingUncrouch = true;

        // Fatigue: increase counter (reduces future slide boosts).
        state.sim.slideFatigueCounter = std::min(state.sim.slideFatigueCounter + 1, tms::k_slideFatigueMax);
        return;
    }

    // Ground jump (or coyote ground jump)
    if (state.vis.grounded || state.sim.coyoteTimer > 0.0f) {
        vel.y = tms::k_jumpSpeed * gravDir;
        state.vis.grounded = false;
        state.sim.coyoteTimer = 0.0f;
        state.vis.jumpCount = 1;
        // DJ availability is whatever ground time accrued before this jump
        // already granted (in step 9). Don't override here.
        state.sim.jumpedThisTick = true;
        state.sim.jumpCooldown = tms::k_doubleJumpCooldown;

        // Set up jump lurch — only re-arm for "fresh" ground jumps. Bhop-chain
        // re-jumps have groundedDuration ≈ 1-2 ticks, which stays well below
        // k_jumpLurchMinGroundedTime, so lurch stays disarmed and doesn't fire
        // the 180 u/s sideways redirect + 12.5 % speed haircut on the next
        // strafe change.
        const bool k_freshGroundJump = state.sim.groundedDuration >= tms::k_jumpLurchMinGroundedTime;
        state.sim.jumpLurchEnabled = (tms::k_enableJumpLurch != 0) && k_freshGroundJump;
        state.sim.jumpLurchTimer = 0.0f;
        state.sim.moveInputsOnJump = moveInput2D(input);
        return;
    }

    // Double jump
    // Requires: (a) re-press of jump key (not held from first jump),
    //           (b) cooldown expired since last jump.
    const bool k_jumpRisingEdge = input.jump && !state.sim.jumpHeldLastTick;
    if (state.sim.canDoubleJump && state.vis.jumpCount < 2 && k_jumpRisingEdge && state.sim.jumpCooldown <= 0.0f) {
        // Reset vertical velocity before applying double jump (feels better than additive).
        // When flipped, "falling" means vel.y > 0 so the check direction inverts.
        if (vel.y * gravDir < 0.0f)
            vel.y = 0.0f;
        vel.y += tms::k_doubleJumpSpeed * gravDir;
        state.sim.canDoubleJump = false;
        state.vis.jumpCount = 2;
        state.sim.jumpedThisTick = true;

        // Horizontal redirect: if any WASD is held, replace horizontal velocity with
        // wishDir * max(boost, currentHorizSpeed). This turns the second jump into an
        // air dash — letting players counter their own first-jump direction (e.g. jump
        // right, then double-jump-left to reverse course). Momentum is preserved if
        // already faster than the boost. With no WASD, horizontal velocity is untouched.
        const glm::vec3 k_djWishDir =
            physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
        if (glm::length(k_djWishDir) > 0.001f) {
            const float k_djHs = horizSpeed(vel);
            const float k_djSpeed = std::max(tms::k_doubleJumpHorizBoost, k_djHs);
            vel.x = k_djWishDir.x * k_djSpeed;
            vel.z = k_djWishDir.z * k_djSpeed;
        }

        // Lurch resets on double jump too — but only if the preceding ground jump
        // was itself "fresh". Since double jump happens mid-air, groundedDuration
        // is always 0 here, so this gate makes double-jump NEVER re-arm lurch.
        // That's intentional: bhop+doublejump chains keep lurch disarmed, and
        // lurch remains the deliberate "direction correction" feature reserved
        // for standing-start jumps only.
        const bool k_freshGroundJump = state.sim.groundedDuration >= tms::k_jumpLurchMinGroundedTime;
        state.sim.jumpLurchEnabled = (tms::k_enableJumpLurch != 0) && k_freshGroundJump;
        state.sim.jumpLurchTimer = 0.0f;
        state.sim.moveInputsOnJump = moveInput2D(input);
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
void handleJumpLurch(glm::vec3& vel, const InputSnapshot& input, PlayerStateRef state)
{
    if constexpr (tms::k_enableJumpLurch == 0) {
        // Jump lurch disabled globally: clear state so timers don't accumulate and bail.
        state.sim.jumpLurchEnabled = false;
        state.sim.jumpLurchTimer = 0.0f;
        return;
    }

    if (!state.sim.jumpLurchEnabled)
        return;

    const glm::vec2 k_currentInput = moveInput2D(input);

    // Only trigger lurch if the player is pressing a DIFFERENT direction than when they jumped.
    if (k_currentInput == state.sim.moveInputsOnJump || glm::length(k_currentInput) < 0.01f)
        return;

    // Lurch strength decays linearly from max at graceMin to 0 at graceMax.
    const float k_t = state.sim.jumpLurchTimer;
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
    state.sim.jumpLurchEnabled = false;
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
void tryEnterSlide(
    glm::vec3& vel, PlayerStateRef state, CollisionShape& shape, Position& pos, const InputSnapshot& input)
{
    if (state.vis.moveMode != MoveMode::OnFoot)
        return;
    if (!input.crouch || !state.vis.grounded || !state.sim.canEnterSlide)
        return;

    const float k_hs = horizSpeed(vel);
    if (k_hs < tms::k_slideMinStartSpeed)
        return;

    // Enter slide.
    state.vis.moveMode = MoveMode::Sliding;
    state.sim.slideTimer = 0.0f;
    state.vis.crouching = true;
    shape.halfExtents.y = tms::k_crouchingHalfHeight;
    pos.value.y -= (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);

    // Slide boost (if not on cooldown and fatigue allows).
    if (state.sim.slideBoostCooldown <= 0.0f) {
        const float k_fatigueScale =
            1.0f - static_cast<float>(state.sim.slideFatigueCounter) / static_cast<float>(tms::k_slideFatigueMax);
        const float k_boost = std::lerp(tms::k_slideBoostMin,
                                        tms::k_slideBoostMax,
                                        std::clamp((k_hs - tms::k_slideMinStartSpeed) / 200.0f, 0.0f, 1.0f));
        const float k_actualBoost = k_boost * std::max(0.0f, k_fatigueScale);

        if (k_actualBoost > 0.0f) {
            // Add boost in the current horizontal direction.
            const glm::vec3 k_horizDir = glm::normalize(horizVel(vel));
            vel += k_horizDir * k_actualBoost;
        }
        state.sim.slideBoostCooldown = tms::k_slideBoostCooldown;
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
    glm::vec3& vel, PlayerStateRef state, CollisionShape& shape, Position& pos, const InputSnapshot& input, float dt)
{
    state.sim.slideTimer += dt;

    // Exit conditions: release crouch, too slow, or airborne.
    const float k_hs = horizSpeed(vel);
    if (!input.crouch || k_hs < tms::k_slideMinSpeed || !state.vis.grounded) {
        state.vis.moveMode = MoveMode::OnFoot;
        if (!input.crouch) {
            state.vis.crouching = false;
            shape.halfExtents.y = tms::k_standingHalfHeight;
            pos.value.y += (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);
        }
        return;
    }

    // Braking deceleration ramps up over slide duration.
    const float k_brakingAlpha = std::clamp(state.sim.slideTimer / tms::k_slideBrakingRampTime, 0.0f, 1.0f);
    const float k_braking = std::lerp(tms::k_slideBrakingDecelMin, tms::k_slideBrakingDecelMax, k_brakingAlpha);

    // Apply braking in the direction of horizontal motion.
    if (k_hs > 0.001f) {
        const float k_newSpeed = std::max(0.0f, k_hs - k_braking * dt);
        const float k_scale = k_newSpeed / k_hs;
        vel.x *= k_scale;
        vel.z *= k_scale;
    }

    // Slight steering: apply lateral wish acceleration so WASD can gently
    // rotate the slide trajectory. The lateral component is wishDir minus
    // its projection onto current motion, so forward input is ignored and
    // only the perpendicular part curves the slide.
    if (k_hs > 0.001f) {
        const glm::vec3 k_wishDir =
            physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
        if (glm::length(k_wishDir) > 0.001f) {
            const glm::vec3 k_horizDir = horizVel(vel) / k_hs;
            const glm::vec3 k_lateral = k_wishDir - k_horizDir * glm::dot(k_wishDir, k_horizDir);
            vel += k_lateral * tms::k_slideSteerAccel * dt;
        }
    }

    // Surface angle influence: slopes accelerate/decelerate the slide.
    // A perfectly flat floor has groundNormal = (0,1,0), slopeForce = 0.
    // Downhill: the gravity component along the slope adds speed.
    // Uphill: it subtracts speed.
    if (state.vis.groundNormal.y < 0.999f && state.vis.groundNormal.y > 0.01f) {
        // Project gravity onto the slope surface to get the slide force direction.
        const glm::vec3 k_gravity{0.0f, -1.0f, 0.0f};
        const glm::vec3 k_slopeDir =
            glm::normalize(k_gravity - state.vis.groundNormal * glm::dot(k_gravity, state.vis.groundNormal));
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
                     PlayerStateRef state,
                     const InputSnapshot& input,
                     const physics::WallDetectionResult& walls,
                     float posY)
{
    if (state.vis.moveMode != MoveMode::OnFoot)
        return;
    if (state.vis.grounded || state.vis.exitingWall)
        return;
    if (walls.groundDistance < tms::k_wallrunMinGroundDist)
        return;
    // Lucio-style: jump must be held to attach. Releasing detaches (handled in
    // handleWallRunning), so attachment requires explicit intent every time.
    if (!input.jump)
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
        if (isBlacklisted(wallNorm,
                          posY,
                          state.sim.wallBlacklistNormal,
                          state.sim.wallBlacklistHeight,
                          state.sim.wallBlacklistActive))
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

        state.vis.moveMode = MoveMode::WallRunning;
        state.vis.wallRunSide = side;
        state.sim.wallNormal = wallNorm;
        state.sim.wallForward = wallFwd;
        state.sim.wallRunTimer = 0.0f;
        state.sim.wallRunSpeedTimer = 0.0f;
        // DJ no longer refreshes from entering wallrun — only from ground time.
        state.vis.jumpCount = 0;

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
void exitWallrun(PlayerStateRef state, float posY)
{
    state.vis.moveMode = MoveMode::OnFoot;
    state.vis.exitingWall = true;
    state.sim.exitWallTimer = tms::k_wallrunExitTime;
    state.sim.wasWallRunning = true;
    state.sim.coyoteTimer = tms::k_coyoteTime;
    state.sim.wallBlacklistActive = true;
    state.sim.wallBlacklistNormal = state.sim.wallNormal;
    state.sim.wallBlacklistHeight = posY;
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
                       PlayerStateRef state,
                       const InputSnapshot& input,
                       const physics::WallDetectionResult& walls,
                       const glm::vec3& halfExtents,
                       float posY,
                       float dt)
{
    state.sim.wallRunTimer += dt;
    state.sim.wallRunSpeedTimer += dt;

    // Lucio-style detach: jump released → fire the full wall-jump impulse
    // away from the wall, then exit. Holding jump is what kept the player
    // attached; releasing it is the explicit "kick off" signal.
    if (!input.jump) {
        const float k_gravDir = state.vis.gravityFlipped ? -1.0f : 1.0f;
        vel.y = tms::k_wallJumpUpForce * k_gravDir;
        vel += state.sim.wallNormal * tms::k_wallJumpSideForce;
        state.vis.jumpCount = 1;
        state.sim.jumpedThisTick = true;
        exitWallrun(state, posY);
        return;
    }

    // Exit: lost wall contact
    const bool k_stillOnWall = (state.vis.wallRunSide == WallSide::Right && walls.wallRight) ||
                               (state.vis.wallRunSide == WallSide::Left && walls.wallLeft);
    if (!k_stillOnWall) {
        exitWallrun(state, posY);
        return;
    }

    // No look-away detach: Lucio glide stays attached regardless of where the
    // player looks. The only ways out are jump release (above) or losing wall
    // contact (e.g. the wall ends).

    // --- Update wall normal from latest detection (curved surface tracking) ---
    if (state.vis.wallRunSide == WallSide::Right)
        state.sim.wallNormal = walls.rightNormal;
    else
        state.sim.wallNormal = walls.leftNormal;

    // --- Curved surface: constrained motion ---
    // On a flat wall, tangential velocity is parallel to the surface.
    // On a curved surface (cylinder), the tangent rotates, so velocity must
    // be re-projected onto the new tangent plane each tick.  Without this,
    // the player flies off tangentially — the push force (300 u/s²) can't
    // overcome the centripetal acceleration needed (v²/r ≈ 2000+ u/s²).

    // 1. Preserve speed, re-project velocity onto the new wall tangent plane.
    //    This is the key step — it rotates the velocity to follow curvature.
    {
        const float k_normalVel = glm::dot(vel, state.sim.wallNormal);
        // Remove the outward component (only if pointing away from wall).
        // Keep inward component so the push force can press the player in.
        if (k_normalVel > 0.0f)
            vel -= state.sim.wallNormal * k_normalVel;
    }

    // 2. Position correction: if the wall contact point is known, nudge the
    //    player toward the wall to maintain consistent standoff distance.
    {
        const glm::vec3 wallPt = (state.vis.wallRunSide == WallSide::Right) ? walls.rightPoint : walls.leftPoint;
        // Vector from wall contact point to player center, along wall normal.
        const float k_currentDist = glm::dot(pos - wallPt, state.sim.wallNormal);
        // Desired standoff: just outside the collision shape.
        const float k_desiredDist = std::max(halfExtents.x, halfExtents.z) + 1.0f;
        const float k_drift = k_currentDist - k_desiredDist;
        // If drifting outward, pull back. Lerp to avoid jitter.
        if (k_drift > 0.5f) {
            const float k_correction = std::min(k_drift * 10.0f * dt, k_drift);
            pos -= state.sim.wallNormal * k_correction;
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
        wallFwd = k_hv - state.sim.wallNormal * glm::dot(k_hv, state.sim.wallNormal);
        const float k_projLen = glm::length(wallFwd);
        if (k_projLen > 0.001f)
            wallFwd /= k_projLen;
        else
            wallFwd = state.sim.wallForward;
    } else {
        wallFwd = state.sim.wallForward;
    }

    // Ensure consistency with stored direction (don't flip 180°).
    if (glm::dot(state.sim.wallForward, wallFwd) < 0.0f)
        wallFwd = -wallFwd;
    state.sim.wallForward = wallFwd;

    // Accelerate along the wall.
    const float k_currentFwdSpeed = glm::dot(k_hv, state.sim.wallForward);
    if (k_currentFwdSpeed < tms::k_wallrunMaxSpeed) {
        const float k_addSpeed = std::min(tms::k_wallrunAccel * dt, tms::k_wallrunMaxSpeed - k_currentFwdSpeed);
        vel += state.sim.wallForward * k_addSpeed;
    }

    // Speed clamping (after initial delay to allow wallkick tech).
    if (state.sim.wallRunSpeedTimer > tms::k_wallrunSpeedLossDelay)
        clampHorizSpeed(vel, tms::k_wallrunMaxSpeed);

    // Push toward wall to keep player stuck.
    vel -= state.sim.wallNormal * tms::k_wallrunPushForce * dt;

    // Pure Lucio glide: gravity never applies while attached. The player can
    // ride for as long as they hold jump and the wall continues. Vertical
    // movement only resumes after detach (release or lost contact).
    vel.y = 0.0f;

    // Camera tilt.
    state.vis.targetCameraTilt =
        (state.vis.wallRunSide == WallSide::Right) ? tms::k_wallrunCameraTilt : -tms::k_wallrunCameraTilt;
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
                   PlayerStateRef state,
                   const InputSnapshot& input,
                   const physics::WallDetectionResult& walls,
                   float posY)
{
    if (state.vis.moveMode != MoveMode::OnFoot)
        return;
    if (state.vis.grounded || state.vis.exitingClimb)
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
                      state.sim.climbBlacklistNormal,
                      state.sim.climbBlacklistHeight,
                      state.sim.climbBlacklistActive))
        return;

    // Enter climbing.
    state.vis.moveMode = MoveMode::Climbing;
    state.sim.climbWallNormal = walls.frontNormal;
    state.sim.climbTimer = 0.0f;
    // DJ no longer refreshes from entering climb — only from ground time.
    state.vis.jumpCount = 0;

    // Reduce horizontal velocity immediately.
    vel.x *= tms::k_climbSidewaysMultiplier;
    vel.z *= tms::k_climbSidewaysMultiplier;
    vel.y = std::max(vel.y, 0.0f);
}

/// @brief Exit climb mode and start cooldown timers.
/// @param state  Player state (modified in place).
/// @param posY   Current vertical position for blacklist height.
void exitClimb(PlayerStateRef state, float posY)
{
    state.vis.moveMode = MoveMode::OnFoot;
    state.vis.exitingClimb = true;
    state.sim.exitClimbTimer = tms::k_climbExitTime;
    state.sim.wasClimbing = true;
    state.sim.coyoteTimer = tms::k_coyoteTime;
    state.sim.climbBlacklistActive = true;
    state.sim.climbBlacklistNormal = state.sim.climbWallNormal;
    state.sim.climbBlacklistHeight = posY;
}

/// @brief Process climbing movement with speed decay and exit conditions.
/// @param vel    Velocity (modified in place).
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot.
/// @param walls  Wall detection result from this tick.
/// @param posY   Current vertical position of the entity.
/// @param dt     Fixed physics delta time in seconds.
void handleClimbing(glm::vec3& vel,
                    PlayerStateRef state,
                    const InputSnapshot& input,
                    const physics::WallDetectionResult& walls,
                    float posY,
                    float dt)
{
    state.sim.climbTimer += dt;

    // Exit conditions
    if (state.sim.climbTimer >= tms::k_climbKickoffDuration || !walls.wallFront || !input.forward) {
        exitClimb(state, posY);
        return;
    }

    // Climbing movement (upward with speed decay)
    const float k_decayAlpha = std::clamp(state.sim.climbTimer / tms::k_climbKickoffDuration, 0.0f, 1.0f);
    const float k_climbSpeed = std::lerp(tms::k_climbMaxSpeed, tms::k_climbMinSpeed, k_decayAlpha);

    vel.y = k_climbSpeed;

    // Minimal sideways movement.
    vel.x *= tms::k_climbSidewaysMultiplier;
    vel.z *= tms::k_climbSidewaysMultiplier;

    // Push toward wall.
    vel -= state.sim.climbWallNormal * tms::k_wallrunPushForce * dt;

    state.vis.targetCameraTilt = 0.0f;
}

} // namespace

// Ledge grabbing

namespace
{

/// @brief Attempt to grab a ledge while climbing.
/// @param state  Player state (modified in place).
/// @param walls  Wall detection result from this tick.
void tryEnterLedgeGrab(PlayerStateRef state, const physics::WallDetectionResult& walls)
{
    // Can only grab ledges while climbing.
    if (state.vis.moveMode != MoveMode::Climbing)
        return;
    if (!walls.ledgeDetected)
        return;
    if (state.sim.exitingLedge)
        return;

    state.vis.moveMode = MoveMode::LedgeGrabbing;
    state.sim.ledgePoint = walls.ledgePoint;
    state.sim.ledgeNormal = walls.ledgeNormal;
    state.sim.ledgeHoldTimer = 0.0f;
    // DJ no longer refreshes from entering ledge grab — only from ground time.
    state.vis.jumpCount = 0;
}

/// @brief Process ledge grab hold, freeze velocity, and auto-mantle.
/// @param vel    Velocity (modified in place).
/// @param state  Player state (modified in place).
/// @param input  Current input snapshot.
/// @param dt     Fixed physics delta time in seconds.
void handleLedgeGrab(glm::vec3& vel, PlayerStateRef state, const InputSnapshot& input, float dt)
{
    state.sim.ledgeHoldTimer += dt;

    // Freeze velocity (gravity is countered).
    vel = glm::vec3(0.0f);

    // Auto-mantle: if holding movement keys past min hold time.
    if (state.sim.ledgeHoldTimer >= tms::k_ledgeMinHoldTime && anyMoveInput(input)) {
        vel.y = tms::k_ledgeJumpUpForce;
        vel += state.sim.ledgeNormal * tms::k_ledgeJumpBackForce;
        state.vis.moveMode = MoveMode::OnFoot;
        state.sim.exitingLedge = true;
        state.sim.exitLedgeTimer = tms::k_ledgeExitTime;
        // DJ no longer refreshes from auto-mantle — only from ground time.
        state.vis.jumpCount = 1;
    }

    state.vis.targetCameraTilt = 0.0f;
}

} // namespace

// Coyote time

namespace
{

/// @brief Start coyote timer when transitioning from grounded to airborne.
/// @param state  Player state (modified in place).
void updateCoyoteTime(PlayerStateRef state)
{
    // Start coyote timer when transitioning from grounded to airborne.
    if (state.sim.wasGroundedLastTick && !state.vis.grounded && state.vis.moveMode == MoveMode::OnFoot &&
        !state.sim.jumpedThisTick)
    {
        state.sim.coyoteTimer = tms::k_coyoteTime;
    }

    state.sim.wasGroundedLastTick = state.vis.grounded;
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

/// @brief End-point of the perch arc for a given hook + player half-height.
///
/// Sits `k_grapplePerchFeetOffset + halfExtentY` directly above the hook
/// point, so the player's feet land `k_grapplePerchFeetOffset` above the
/// hook (typically on top of the platform that owns the hooked corner).
glm::vec3 perchEndPoint(glm::vec3 hookPoint, float halfExtentY)
{
    return hookPoint + glm::vec3(0.0f, tms::k_grapplePerchFeetOffset + halfExtentY, 0.0f);
}

/// @brief Detach the grapple and apply the look-biased launch impulse.
///
/// The launch velocity is a blend of the grapple-line direction and the
/// player's current look direction.  Looking upward near detach converts
/// horizontal pull speed into a soaring vertical arc — this is the core
/// Widowmaker tech that makes the grapple expressive.
void grappleDetachWithLaunch(glm::vec3& vel, PlayerStateRef state, const InputSnapshot& input, glm::vec3 pos)
{
    const float k_speed = glm::length(vel);

    // Current grapple-line direction (from player toward anchor).
    const glm::vec3 k_toHook = state.vis.grapplePoint - pos;
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
    state.vis.grappleActive = false;
    state.sim.grappleCooldownActive = true;
    state.sim.grappleCooldownTimer = tms::k_grappleCooldown;
    state.vis.grounded = false;
}

/// @brief Fire grapple on E press.  One-shot: press E to fire, not hold.
void tryFireGrapple(PlayerStateRef state,
                    const InputSnapshot& input,
                    glm::vec3 eye,
                    const physics::WorldGeometry& world)
{
    const bool k_pressed = input.grapple && !state.sim.grappleInputLastTick;
    if (!k_pressed || state.vis.grappleActive || state.sim.grappleCooldownActive)
        return;

    // Raycast forward from the eye position.
    const glm::vec3 k_fwd = lookDirFromInput(input);
    const glm::vec3 k_end = eye + k_fwd * tms::k_grappleMaxRange;
    const physics::SphereHitResult k_hit = physics::sphereCast(4.0f, eye, k_end, world);

    if (!k_hit.hit)
        return;

    // Hook attached — begin pull.
    state.vis.grappleActive = true;
    state.sim.grapplePullTimer = 0.0f;
    state.vis.grapplePoint = k_hit.point;
    state.sim.grapplePullDir = glm::normalize(k_hit.point - eye);

    // Force airborne — the grapple lifts you off the ground immediately.
    state.vis.grounded = false;
}

/// @brief Pull the player toward the anchor, or arc-perch above it if jump is held.
///
/// Two modes:
/// - **Default (jump not held):** velocity is overridden each tick to point
///   directly at the anchor at `k_grapplePullSpeed`. Pure linear flight,
///   no gravity, no air control. Crouch cancels (drop). Arrival or timeout
///   triggers a look-biased launch.
/// - **Perch (jump held):** the end-point shifts to `perchEndPoint` (above
///   the hook by `k_grapplePerchFeetOffset + halfExtentY`) and the player
///   follows a quadratic Bezier whose control point sits at the end-point's
///   vertical level, midway horizontally. Result: an upward arc that lands
///   you on top of the platform whose corner you hooked. No launch on
///   arrival — you drop the last few units onto the surface.
void handleGrapple(glm::vec3& vel,
                   PlayerStateRef state,
                   const InputSnapshot& input,
                   glm::vec3 pos,
                   const CollisionShape& shape,
                   float /*dt*/)
{
    if (!state.vis.grappleActive)
        return;

    // Crouch always cancels — no-launch drop. Same in both modes.
    if (input.crouch) {
        state.vis.grappleActive = false;
        state.sim.grappleCooldownActive = true;
        state.sim.grappleCooldownTimer = tms::k_grappleCooldown;
        return;
    }

    // ── Perch mode: held jump → stateless rise-then-traverse arc ───────
    //
    // The trajectory is computed each tick from current pos + replicated
    // grapplePoint only. No per-tick accumulator (elapsed time, saved
    // start position, bezier `t` parameter) — those would live in
    // PlayerSimState which is server-only, drift on every reconciliation
    // replay, and cause visible position jitter.
    //
    // Shape: vertical pull is a P-controller toward perchEnd.y, capped at
    // pullSpeed; horizontal speed is reduced while still significantly
    // below the target so the player rises before traversing — gives the
    // perceived "arc up and over" feel of the original bezier without
    // needing any state.
    if (input.jump) {
        const glm::vec3 k_perchEnd = perchEndPoint(state.vis.grapplePoint, shape.halfExtents.y);
        const glm::vec3 k_toTarget = k_perchEnd - pos;
        const glm::vec2 k_horizDelta{k_toTarget.x, k_toTarget.z};
        const float k_horizDist = glm::length(k_horizDelta);

        // Below-target XZ throttle: full pullSpeed at/above target altitude,
        // dropping to k_minHorizFactor when k_grapplePerchRiseRange below it.
        const float k_belowness = std::max(0.0f, k_toTarget.y) / tms::k_grapplePerchRiseRange;
        const float k_horizFactor = std::clamp(1.0f - k_belowness, tms::k_grapplePerchMinHorizFactor, 1.0f);

        glm::vec3 k_v{0.0f, 0.0f, 0.0f};
        if (k_horizDist > 0.001f) {
            const float k_hSpeed = tms::k_grapplePullSpeed * k_horizFactor;
            k_v.x = (k_horizDelta.x / k_horizDist) * k_hSpeed;
            k_v.z = (k_horizDelta.y / k_horizDist) * k_hSpeed;
        }
        // Vertical P-controller, capped at pullSpeed.
        k_v.y = std::clamp(
            k_toTarget.y * tms::k_grapplePerchVerticalGain, -tms::k_grapplePullSpeed, tms::k_grapplePullSpeed);

        vel = k_v;

        // Arrival: close enough to perch end → land (no launch).
        if (glm::length(k_toTarget) < tms::k_grappleDetachDist) {
            state.vis.grappleActive = false;
            state.sim.grappleCooldownActive = true;
            state.sim.grappleCooldownTimer = tms::k_grappleCooldown;
            state.vis.grounded = false;
        }
        return;
    }

    const glm::vec3 k_toHook = state.vis.grapplePoint - pos;
    const float k_dist = glm::length(k_toHook);

    // ── Detach conditions (default linear-pull mode) ────────────────────

    // Arrived at anchor — auto-detach with launch.
    if (k_dist < tms::k_grappleDetachDist) {
        grappleDetachWithLaunch(vel, state, input, pos);
        return;
    }

    // Safety timeout.
    if (state.sim.grapplePullTimer > tms::k_grappleMaxDuration) {
        grappleDetachWithLaunch(vel, state, input, pos);
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
void applySpeedCap(glm::vec3& vel, const PlayerStateRef state)
{
    // During grapple pull, velocity is directly controlled — don't cap it.
    if (state.vis.grappleActive)
        return;
    clampHorizSpeed(vel, tms::k_speedCap);
}

} // namespace

// Main entry point

void runMovement(Registry& registry, float dt, const physics::WorldGeometry& world)
{
    // Phase 6: drain force / impulse accumulators into velocities before the
    // per-entity kernel runs.  No-op for entities without `RigidBody` — the
    // legacy direct-velocity-mutation pattern (e.g. for kinematic players)
    // is unaffected.  This is the prerequisite path for dynamic bodies.
    physics::forces::integrateAccumulators(registry, dt);

    // Entities WITH InputSnapshot — full player movement.
    //
    // PR-7 (server-perf): per-player movement is independent — each
    // iteration only reads `WorldGeometry` (read-only, shared) and
    // mutates only its own entity's components. Pre-collect entity
    // handles, then run the kernel via parallelFor on TBB-backed
    // builds. With AI bots actually moving, this scope's CPU work
    // grows linearly with N; at 500 it would dominate the tick.
    auto playerView = registry.view<Position, Velocity, PlayerVisState, PlayerSimState, CollisionShape, InputSnapshot>(
        entt::exclude<RespawnTimer>);
    static thread_local std::vector<entt::entity> moveWork;
    moveWork.clear();
    for (auto e : playerView)
        moveWork.push_back(e);

    auto moveKernel = [&registry, dt, &world](entt::entity e) {
        auto& pos = registry.get<Position>(e);
        auto& vel = registry.get<Velocity>(e);
        auto& vis = registry.get<PlayerVisState>(e);
        auto& sim = registry.get<PlayerSimState>(e);
        auto& shape = registry.get<CollisionShape>(e);
        const auto& input = registry.get<InputSnapshot>(e);
        {
            // Bundle the two halves into a single ref so the helper functions
            // below don't need a "(vis, sim)" pair on every call.
            PlayerStateRef state{vis, sim};
            // 0. Tick timers
            tickTimers(state, dt);

            // 0b. Gravity flip toggle (rising edge of G key with cooldown).
            {
                const bool flipEdge = input.flipGravity && !state.sim.flipGravityHeldLastTick;
                if (flipEdge && state.sim.gravityFlipCooldown <= 0.0f) {
                    state.vis.gravityFlipped = !state.vis.gravityFlipped;
                    state.sim.gravityFlipCooldown = physics::k_gravityFlipCooldown;
                    // Clear grounded so the player doesn't stick to the old surface.
                    state.vis.grounded = false;
                }
                state.sim.flipGravityHeldLastTick = input.flipGravity;
            }

            // Gravity direction multiplier: +1 normal, -1 flipped.
            const float gravDir = state.vis.gravityFlipped ? -1.0f : 1.0f;

            // 1. Wall / climb / ledge detection
            // Pass the previous wall normal so the detector can trace toward
            // curved surfaces (cylinders, concave walls) whose normal rotates
            // as the player moves along them.
            physics::WallDetectionResult walls{};
            if (!state.vis.grounded || state.vis.moveMode == MoveMode::WallRunning ||
                state.vis.moveMode == MoveMode::Climbing)
            {
                const glm::vec3 prevNormal =
                    (state.vis.moveMode == MoveMode::WallRunning) ? state.sim.wallNormal : glm::vec3(0.0f);
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
            if (state.vis.moveMode == MoveMode::OnFoot)
                tryEnterClimb(vel.value, state, input, walls, pos.value.y);
            if (state.vis.moveMode == MoveMode::OnFoot)
                tryEnterWallrun(vel.value, state, input, walls, pos.value.y);
            if (state.vis.moveMode == MoveMode::OnFoot)
                tryEnterSlide(vel.value, state, shape, pos, input);

            // 4. Crouch transition (only in OnFoot)
            if (state.vis.moveMode == MoveMode::OnFoot)
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
            //     jump-detach internally). `state.vis.grappleActive` here reflects
            //     the previous tick's grapple state, since tryFireGrapple runs
            //     after mode-specific movement below — acceptable because
            //     grapple-rising-edge + jump same-tick is a rare edge case and
            //     handleGrapple's velocity override wins anyway.
            if (!state.vis.grappleActive)
                handleJump(vel.value, input, state, dt, gravDir);

            // 5. Mode-specific movement
            switch (state.vis.moveMode) {
            case MoveMode::OnFoot: {
                const glm::vec3 k_wishDir =
                    physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
                const float k_wishSpeed = currentWishSpeed(state.vis);

                if (state.vis.grounded) {
                    vel.value = physics::applyGroundFriction(vel.value, dt);
                    if (glm::length(k_wishDir) > 0.001f)
                        vel.value = physics::accelerate(vel.value, k_wishDir, k_wishSpeed, physics::k_groundAccel, dt);
                } else {
                    vel.value = physics::applyGravity(vel.value, dt, state.vis.gravityFlipped);
                    if (glm::length(k_wishDir) > 0.001f) {
                        // Air wish-speed depends on current horizontal speed:
                        // generous when stalled, classic Quake floor at speed.
                        const float k_airWish = physics::airWishSpeedForHorizSpeed(horizSpeed(vel.value));
                        vel.value = physics::accelerate(vel.value, k_wishDir, k_airWish, physics::k_airAccel, dt);
                    }
                }

                // Camera tilt returns to zero.
                state.vis.targetCameraTilt = 0.0f;
                break;
            }

            case MoveMode::Sliding: {
                handleSliding(vel.value, state, shape, pos, input, dt);
                // Slide camera lean: tilt based on lateral velocity relative to look direction.
                const float k_sinY = std::sin(input.yaw);
                const float k_cosY = std::cos(input.yaw);
                const glm::vec3 k_lookRight{k_cosY, 0.0f, -k_sinY};
                const float k_lateralSpeed = glm::dot(horizVel(vel.value), k_lookRight);
                state.vis.targetCameraTilt = std::clamp(k_lateralSpeed / 400.0f * 3.0f, -5.0f, 5.0f);
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
                const float grapEyeDir = state.vis.gravityFlipped ? -1.0f : 1.0f;
                const glm::vec3 k_eye = pos.value + glm::vec3(0, shape.halfExtents.y * 0.77f * grapEyeDir, 0);
                tryFireGrapple(state, input, k_eye, world);
                state.sim.grappleInputLastTick = input.grapple;

                if (state.vis.grappleActive) {
                    // Cancel wallrun/climb/slide on grapple.
                    if (state.vis.moveMode != MoveMode::OnFoot) {
                        state.vis.moveMode = MoveMode::OnFoot;
                        if (state.vis.crouching)
                            state.vis.pendingUncrouch = true;
                    }

                    // Pull overrides velocity. Jump = perch arc, crouch = drop, both
                    // handled inside handleGrapple.
                    handleGrapple(vel.value, state, input, pos.value, shape, dt);

                    // If still active after handleGrapple, mark as pulling
                    // to skip all remaining movement this tick.
                    grapplePulling = state.vis.grappleActive;
                }
            }

            // 6. Jump handling is now step 4b (before mode-specific movement).
            //    See the comment there for the bhop-preservation rationale.

            // 7. Jump lurch (air only, NOT during grapple)
            if (!grapplePulling && !state.vis.grounded && state.vis.moveMode == MoveMode::OnFoot)
                handleJumpLurch(vel.value, input, state);

            // 8. Coyote time update
            if (!grapplePulling)
                updateCoyoteTime(state);

            // 9. Landing reset
            if (state.vis.grounded && state.vis.moveMode == MoveMode::OnFoot) {
                state.vis.jumpCount = 0;
                state.sim.canEnterSlide = true;
                state.sim.jumpLurchEnabled = false;

                // Double jump: refresh only after enough continuous OnFoot time
                // on the ground. This is the *only* path that grants DJ — wall
                // jumps, climb jumps, slidehops, and ledge mantles deliberately
                // don't restore it.
                if (state.sim.groundedDuration >= tms::k_doubleJumpGroundedRefreshTime)
                    state.sim.canDoubleJump = true;

                // Clear blacklists on landing.
                state.sim.wallBlacklistActive = false;
                state.sim.climbBlacklistActive = false;
            }

            // 10. Auto-uncrouch / pending uncrouch
            // If the player should uncrouch (slidehop exit, or crouch key
            // released while crouched) but collision might block it, we
            // try here. The collision system's depenetration will push us
            // back down if expanding the AABB puts us inside geometry.
            if (state.vis.pendingUncrouch ||
                (state.vis.crouching && !input.crouch && state.vis.moveMode == MoveMode::OnFoot))
            {
                // Try to stand up: expand AABB and raise centre.
                state.vis.crouching = false;
                shape.halfExtents.y = tms::k_standingHalfHeight;
                pos.value.y += (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);

                // Check if standing up puts us inside geometry.
                // Use a quick sweep upward from current pos — if it hits
                // immediately, we can't stand and must stay crouched.
                const glm::vec3 k_testEnd = pos.value + glm::vec3(0, 0.1f, 0);
                const physics::HitResult k_test = physics::sweepAll(shape.halfExtents, pos.value, k_testEnd, world);
                if (k_test.hit && k_test.tFirst < 0.01f) {
                    // Can't stand — revert to crouched.
                    state.vis.crouching = true;
                    shape.halfExtents.y = tms::k_crouchingHalfHeight;
                    pos.value.y -= (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);
                }
                state.vis.pendingUncrouch = false;
            }

            // 11. Speed cap
            applySpeedCap(vel.value, state);

            // 12. Track jump key state for edge detection
            state.sim.jumpHeldLastTick = input.jump;
        }
    };

#if GROUP2_MOVEMENT_HAS_PARALLEL
    ::group2::perf::parallelFor(moveWork.begin(), moveWork.end(), moveKernel);
#else
    for (entt::entity e : moveWork)
        moveKernel(e);
#endif

    // Projectile integration (gravity, drag, etc.) is handled inside CollisionSystem's
    // projectile loop so it stays interleaved with the swept-AABB bump loop. See
    // CollisionSystem.cpp for the gravity application gated on isGrenadeType().
}

} // namespace systems
