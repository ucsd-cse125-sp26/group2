#include "ecs/systems/MovementSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
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

// ═══════════════════════════════════════════════════════════════════════════
// Helper utilities
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

/// Horizontal speed (XZ plane).
float horizSpeed(const glm::vec3& v)
{
    return std::sqrt(v.x * v.x + v.z * v.z);
}

/// Horizontal velocity (Y zeroed).
glm::vec3 horizVel(const glm::vec3& v)
{
    return {v.x, 0.0f, v.z};
}

/// Clamp horizontal speed without affecting Y.
void clampHorizSpeed(glm::vec3& v, float maxSpeed)
{
    const float k_hs = horizSpeed(v);
    if (k_hs > maxSpeed && k_hs > 0.001f) {
        const float k_scale = maxSpeed / k_hs;
        v.x *= k_scale;
        v.z *= k_scale;
    }
}

/// Current WASD input as a 2D vector (X=strafe, Y=forward).
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

bool anyMoveInput(const InputSnapshot& input)
{
    return input.forward || input.back || input.left || input.right;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Crouch / shape transition
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

void handleCrouchTransition(Position& pos, CollisionShape& shape, PlayerState& state, const InputSnapshot& input)
{
    const bool k_wantsCrouch = input.crouch;
    const bool k_isCrouched = state.crouching;

    if (k_wantsCrouch && !k_isCrouched) {
        state.crouching = true;
        shape.halfExtents.y = tms::k_crouchingHalfHeight;
        pos.value.y -= (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);
    } else if (!k_wantsCrouch && k_isCrouched && state.moveMode != MoveMode::Sliding) {
        state.crouching = false;
        shape.halfExtents.y = tms::k_standingHalfHeight;
        pos.value.y += (tms::k_standingHalfHeight - tms::k_crouchingHalfHeight);
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Timer updates (run every tick regardless of mode)
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

void tickTimers(PlayerState& state, float dt)
{
    state.jumpedThisTick = false;

    // Coyote time countdown.
    if (state.coyoteTimer > 0.0f)
        state.coyoteTimer -= dt;

    // Jump lurch timer.
    if (state.jumpLurchEnabled) {
        state.jumpLurchTimer += dt;
        if (state.jumpLurchTimer >= tms::k_jumpLurchGraceMax)
            state.jumpLurchEnabled = false;
    }

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
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Sprint
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

void updateSprint(PlayerState& state, const InputSnapshot& input)
{
    if (input.sprint && input.forward && !state.crouching && state.grounded && state.moveMode == MoveMode::OnFoot) {
        state.sprinting = true;
    } else if (!input.sprint || !input.forward || state.crouching || !state.grounded) {
        state.sprinting = false;
    }
}

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

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Jumping (ground, double, coyote, wall, climb, ledge, slidehop)
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

void handleJump(glm::vec3& vel, const InputSnapshot& input, PlayerState& state, float /*dt*/)
{
    if (!input.jump)
        return;

    // ── Ledge jump / mantle ─────────────────────────────────────────────
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

    // ── Wall jump ───────────────────────────────────────────────────────
    if (state.moveMode == MoveMode::WallRunning) {
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

    // ── Climb jump ──────────────────────────────────────────────────────
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

    // ── Coyote wall jump (off wall within grace period) ─────────────────
    if (!state.grounded && state.coyoteTimer > 0.0f && state.wasWallRunning) {
        vel.y = tms::k_wallJumpUpForce;
        vel += state.wallBlacklistNormal * tms::k_wallJumpSideForce;
        state.coyoteTimer = 0.0f;
        state.wasWallRunning = false;
        state.canDoubleJump = true;
        state.jumpCount = 1;
        state.jumpedThisTick = true;
        return;
    }

    // ── Slidehop ────────────────────────────────────────────────────────
    if (state.moveMode == MoveMode::Sliding) {
        vel.y = tms::k_slidehopJumpSpeed;
        state.moveMode = MoveMode::OnFoot;
        state.crouching = false;
        state.grounded = false;
        state.jumpCount = 1;
        state.canDoubleJump = true;
        state.jumpedThisTick = true;

        // Fatigue: increase counter (reduces future slide boosts).
        state.slideFatigueCounter = std::min(state.slideFatigueCounter + 1, tms::k_slideFatigueMax);
        return;
    }

    // ── Ground jump (or coyote ground jump) ─────────────────────────────
    if (state.grounded || state.coyoteTimer > 0.0f) {
        vel.y = tms::k_jumpSpeed;
        state.grounded = false;
        state.coyoteTimer = 0.0f;
        state.jumpCount = 1;
        state.canDoubleJump = true;
        state.jumpedThisTick = true;

        // Set up jump lurch.
        state.jumpLurchEnabled = true;
        state.jumpLurchTimer = 0.0f;
        state.moveInputsOnJump = moveInput2D(input);
        return;
    }

    // ── Double jump ─────────────────────────────────────────────────────
    if (state.canDoubleJump && state.jumpCount < 2) {
        // Reset vertical velocity before applying double jump (feels better than additive).
        if (vel.y < 0.0f)
            vel.y = 0.0f;
        vel.y += tms::k_doubleJumpSpeed;
        state.canDoubleJump = false;
        state.jumpCount = 2;
        state.jumpedThisTick = true;

        // Lurch resets on double jump too.
        state.jumpLurchEnabled = true;
        state.jumpLurchTimer = 0.0f;
        state.moveInputsOnJump = moveInput2D(input);
    }
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Jump Lurch — directional correction window after jumping
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

void handleJumpLurch(glm::vec3& vel, const InputSnapshot& input, PlayerState& state)
{
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

// ═══════════════════════════════════════════════════════════════════════════
// Sliding
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

/// Try to enter slide: must be moving fast enough + pressing crouch while grounded.
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

    // No ground friction during slide — the braking deceleration handles slowdown.
    // Floor slope influence is handled by the collision system's velocity clipping.
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Wallrunning
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

/// Check if a wall normal matches the blacklist (same wall).
bool isBlacklisted(const glm::vec3& normal, float height, const glm::vec3& blNormal, float blHeight, bool active)
{
    if (!active)
        return false;
    // Same wall = similar normal. Must be at a lower height to regrab.
    if (glm::dot(normal, blNormal) > 0.9f && height >= blHeight)
        return true;
    return false;
}

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
    if (!input.forward) // must hold W
        return;
    if (walls.groundDistance < tms::k_wallrunMinGroundDist)
        return;

    // Check each side.
    auto tryWall = [&](bool hasWall, const glm::vec3& wallNorm, WallSide side) {
        if (!hasWall)
            return false;
        if (isBlacklisted(
                wallNorm, posY, state.wallBlacklistNormal, state.wallBlacklistHeight, state.wallBlacklistActive))
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

        // Reduce vertical velocity to near-zero for a smooth wall-grab feel.
        vel.y = std::clamp(vel.y, -25.0f, 25.0f);

        return true;
    };

    if (!tryWall(walls.wallRight, walls.rightNormal, WallSide::Right))
        tryWall(walls.wallLeft, walls.leftNormal, WallSide::Left);
}

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

void handleWallRunning(glm::vec3& vel,
                       PlayerState& state,
                       const InputSnapshot& input,
                       const physics::WallDetectionResult& walls,
                       float posY,
                       float dt)
{
    state.wallRunTimer += dt;
    state.wallRunSpeedTimer += dt;

    // ── Exit conditions ─────────────────────────────────────────────────
    if (state.wallRunTimer >= tms::k_wallrunKickoffDuration) {
        exitWallrun(state, posY);
        return;
    }

    const bool k_stillOnWall = (state.wallRunSide == WallSide::Right && walls.wallRight) ||
                               (state.wallRunSide == WallSide::Left && walls.wallLeft);
    if (!k_stillOnWall || !input.forward) {
        exitWallrun(state, posY);
        return;
    }

    // ── Movement along wall ─────────────────────────────────────────────
    // Update wall normal from latest detection.
    if (state.wallRunSide == WallSide::Right)
        state.wallNormal = walls.rightNormal;
    else
        state.wallNormal = walls.leftNormal;

    // Recompute wall forward.
    const glm::vec3 k_up{0, 1, 0};
    glm::vec3 wallFwd = glm::cross(k_up, state.wallNormal);
    if (state.wallRunSide == WallSide::Left)
        wallFwd = -wallFwd;
    if (glm::dot(state.wallForward, wallFwd) < 0.0f)
        wallFwd = -wallFwd;
    state.wallForward = wallFwd;

    // Accelerate along the wall.
    const float k_currentFwdSpeed = glm::dot(horizVel(vel), state.wallForward);
    if (k_currentFwdSpeed < tms::k_wallrunMaxSpeed) {
        const float k_addSpeed = std::min(tms::k_wallrunAccel * dt, tms::k_wallrunMaxSpeed - k_currentFwdSpeed);
        vel += state.wallForward * k_addSpeed;
    }

    // Speed clamping (after initial delay to allow wallkick tech).
    if (state.wallRunSpeedTimer > tms::k_wallrunSpeedLossDelay)
        clampHorizSpeed(vel, tms::k_wallrunMaxSpeed);

    // Push toward wall to keep player stuck.
    vel -= state.wallNormal * tms::k_wallrunPushForce * dt;

    // Zero gravity while wallrunning.
    vel.y = 0.0f;

    // Camera tilt.
    state.targetCameraTilt =
        (state.wallRunSide == WallSide::Right) ? -tms::k_wallrunCameraTilt : tms::k_wallrunCameraTilt;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Climbing
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

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

void handleClimbing(glm::vec3& vel,
                    PlayerState& state,
                    const InputSnapshot& input,
                    const physics::WallDetectionResult& walls,
                    float posY,
                    float dt)
{
    state.climbTimer += dt;

    // ── Exit conditions ─────────────────────────────────────────────────
    if (state.climbTimer >= tms::k_climbKickoffDuration || !walls.wallFront || !input.forward) {
        exitClimb(state, posY);
        return;
    }

    // ── Climbing movement (upward with speed decay) ─────────────────────
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

// ═══════════════════════════════════════════════════════════════════════════
// Ledge grabbing
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

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

// ═══════════════════════════════════════════════════════════════════════════
// Coyote time
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

void updateCoyoteTime(PlayerState& state)
{
    // Start coyote timer when transitioning from grounded to airborne.
    if (state.wasGroundedLastTick && !state.grounded && state.moveMode == MoveMode::OnFoot && !state.jumpedThisTick) {
        state.coyoteTimer = tms::k_coyoteTime;
    }

    state.wasGroundedLastTick = state.grounded;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Speed cap
// ═══════════════════════════════════════════════════════════════════════════

namespace
{

void applySpeedCap(glm::vec3& vel)
{
    clampHorizSpeed(vel, tms::k_speedCap);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
// Main entry point
// ═══════════════════════════════════════════════════════════════════════════

void runMovement(Registry& registry, float dt, const physics::WorldGeometry& world)
{
    // ── Entities WITH InputSnapshot — full player movement ──────────────
    registry.view<Position, Velocity, PlayerState, CollisionShape, InputSnapshot>().each(
        [dt,
         &world](Position& pos, Velocity& vel, PlayerState& state, CollisionShape& shape, const InputSnapshot& input) {
            // ── 0. Tick timers ──────────────────────────────────────────
            tickTimers(state, dt);

            // ── 1. Wall / climb / ledge detection ───────────────────────
            physics::WallDetectionResult walls{};
            if (!state.grounded || state.moveMode == MoveMode::WallRunning || state.moveMode == MoveMode::Climbing) {
                walls = physics::detectWalls(pos.value,
                                             input.yaw,
                                             shape.halfExtents,
                                             world,
                                             tms::k_wallrunCheckDist,
                                             tms::k_wallrunSphereRadius);
            }

            // ── 2. Sprint update ────────────────────────────────────────
            updateSprint(state, input);

            // ── 3. State transitions (try enter new modes) ──────────────
            // Order matters: ledge > climb > wallrun > slide
            tryEnterLedgeGrab(state, walls);
            if (state.moveMode == MoveMode::OnFoot)
                tryEnterClimb(vel.value, state, input, walls, pos.value.y);
            if (state.moveMode == MoveMode::OnFoot)
                tryEnterWallrun(vel.value, state, input, walls, pos.value.y);
            if (state.moveMode == MoveMode::OnFoot)
                tryEnterSlide(vel.value, state, shape, pos, input);

            // ── 4. Crouch transition (only in OnFoot) ───────────────────
            if (state.moveMode == MoveMode::OnFoot)
                handleCrouchTransition(pos, shape, state, input);

            // ── 5. Mode-specific movement ───────────────────────────────
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

            case MoveMode::Sliding:
                handleSliding(vel.value, state, shape, pos, input, dt);
                state.targetCameraTilt = 0.0f;
                break;

            case MoveMode::WallRunning:
                handleWallRunning(vel.value, state, input, walls, pos.value.y, dt);
                break;

            case MoveMode::Climbing:
                handleClimbing(vel.value, state, input, walls, pos.value.y, dt);
                break;

            case MoveMode::LedgeGrabbing:
                handleLedgeGrab(vel.value, state, input, dt);
                break;
            }

            // ── 6. Jump handling (works in any mode) ────────────────────
            handleJump(vel.value, input, state, dt);

            // ── 7. Jump lurch (air only) ────────────────────────────────
            if (!state.grounded && state.moveMode == MoveMode::OnFoot)
                handleJumpLurch(vel.value, input, state);

            // ── 8. Coyote time update ───────────────────────────────────
            updateCoyoteTime(state);

            // ── 9. Landing reset ────────────────────────────────────────
            if (state.grounded && state.moveMode == MoveMode::OnFoot) {
                state.jumpCount = 0;
                state.canDoubleJump = true;
                state.canEnterSlide = true;
                state.jumpLurchEnabled = false;

                // Clear blacklists on landing.
                state.wallBlacklistActive = false;
                state.climbBlacklistActive = false;
            }

            // ── 10. Speed cap ───────────────────────────────────────────
            applySpeedCap(vel.value);
        });

    // ── Entities WITHOUT InputSnapshot — physics only (NPCs, etc.) ──────
    registry.view<Velocity, PlayerState>(entt::exclude<InputSnapshot>)
        .each([dt](Velocity& vel, const PlayerState& state) {
            if (state.grounded)
                vel.value = physics::applyGroundFriction(vel.value, dt);
            else
                vel.value = physics::applyGravity(vel.value, dt);
        });
}

} // namespace systems
