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
#include "ecs/physics/PhaseDiagnostic.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/PhysicsPerfStats.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/TriMeshCollision.hpp"
#include "ecs/physics/WallDetection.hpp"

#include <algorithm>
#include <array>
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

bool finiteVec3(glm::vec3 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool usableNormal(glm::vec3 n)
{
    const float lenSq = glm::dot(n, n);
    return finiteVec3(n) && std::isfinite(lenSq) && lenSq > 1e-8f;
}

glm::vec3 normalizedOrZero(glm::vec3 n)
{
    return usableNormal(n) ? n / glm::length(n) : glm::vec3{0.0f};
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

} // namespace

// Crouch / shape transition

namespace
{

/// @brief Resize the player's collision shape to a target AABB half-height,
/// keeping the foot Y in place by translating `pos` along the gravity axis.
///
/// Updates BOTH the AABB half-extent (used by BVH culling and projectile
/// queries) AND the capsule `halfHeight` (used by every player collision
/// path post-rewrite).  The capsule `radius` is held constant — modern
/// KCCs rely on a fixed cross-section radius to keep horizontal contact
/// continuity stable across stance transitions (mid-motion radius changes
/// cause "pop into the wall" artefacts).
void resizePlayerCapsule(Position& pos, CollisionShape& shape, float newAabbHalfHeight)
{
    const float k_oldAabbHalfHeight = shape.halfExtents.y;
    const float k_dy = newAabbHalfHeight - k_oldAabbHalfHeight;
    shape.halfExtents.y = newAabbHalfHeight;
    shape.halfHeight = newAabbHalfHeight - shape.radius;
    pos.value.y += k_dy;
}

/// @brief Test whether the player can fit at `pos` with a candidate AABB
/// half-height, by checking capsule clearance against world geometry.
/// Used to validate uncrouch before committing to the larger shape.
bool playerFitsAt(const Position& pos,
                  const CollisionShape& shape,
                  float candidateAabbHalfHeight,
                  const physics::WorldGeometry& world)
{
    physics::CapsuleShape probe{
        .radius = shape.radius,
        .halfHeight = candidateAabbHalfHeight - shape.radius,
        .up = glm::vec3{0.0f, 1.0f, 0.0f},
    };
    const float k_dy = candidateAabbHalfHeight - shape.halfExtents.y;
    const glm::vec3 probePos = pos.value + glm::vec3{0.0f, k_dy, 0.0f};
    const physics::ClearanceResult clr = physics::clearanceCapsuleVsWorld(probe, probePos, world);
    return clr.distance > -0.03125f; // allow grazing contact within pushback
}

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
        resizePlayerCapsule(pos, shape, tms::k_crouchingHalfHeight);
    }
    // Uncrouch is handled by the auto-uncrouch pass at tick end (step 10)
    // which checks for collision before expanding the capsule.
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

    // Exit-wall timer.
    if (state.vis.exitingWall) {
        state.sim.exitWallTimer -= dt;
        if (state.sim.exitWallTimer <= 0.0f) {
            state.vis.exitingWall = false;
            state.sim.wasWallRunning = false;
        }
    }

    // Grapple cooldown.
    if (state.sim.grappleCooldownActive) {
        state.sim.grappleCooldownTimer -= dt;
        if (state.sim.grappleCooldownTimer <= 0.0f)
            state.sim.grappleCooldownActive = false;
    }
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
    // ADS takes priority — even crouching ADS clamps to the (slower) ADS speed.
    if (vis.ads)
        return tms::k_adsSpeed;
    if (vis.crouching)
        return tms::k_crouchSpeed;
    if (vis.sprinting)
        return tms::k_sprintSpeed;
    return tms::k_walkSpeed;
}

// Jumping (ground, double, coyote, wall, slidehop)

namespace
{

/// @brief Handle all jump types: ground, double, coyote, wall, slidehop.
/// @param vel     Velocity (modified in place).
/// @param input   Current input snapshot.
/// @param state   Player state (modified in place).
/// @param dt      Fixed physics delta time in seconds (unused).
/// @param gravDir +1.0 for normal gravity, -1.0 for flipped (inverts all vertical impulses).
void handleJump(glm::vec3& vel, const InputSnapshot& input, PlayerStateRef state, float /*dt*/, float gravDir = 1.0f)
{
    if (!input.jump)
        return;

    // Wall-jump is no longer fired on jump PRESS while attached. Lucio-style:
    // releasing jump while wallrunning fires the impulse (see handleWallRunning).
    // We don't fall through to other branches because being in WallRunning means
    // no ground/double-jump should fire either.
    if (state.vis.moveMode == MoveMode::WallRunning)
        return;

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
    resizePlayerCapsule(pos, shape, tms::k_crouchingHalfHeight);

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
void handleSliding(glm::vec3& vel, PlayerStateRef state, const InputSnapshot& input, float dt)
{
    state.sim.slideTimer += dt;

    // Exit conditions: release crouch, too slow, or airborne.
    const float k_hs = horizSpeed(vel);
    if (!input.crouch || k_hs < tms::k_slideMinSpeed || !state.vis.grounded) {
        state.vis.moveMode = MoveMode::OnFoot;
        if (!input.crouch) {
            // Defer stand-up to the auto-uncrouch validator at tick end;
            // it does the proper clearance check (we don't know `world`
            // here in this slice).  Marking pendingUncrouch is sufficient
            // because the slide already cleared MoveMode.
            state.vis.pendingUncrouch = true;
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

constexpr float k_wallrunAttachPushback = 0.03125f;
constexpr float k_wallrunCornerClearancePadding = 1.0f;
constexpr float k_wallrunCornerMaxTransitionTime = 0.22f;
constexpr float k_wallrunCornerIntoOldWallDot = -0.25f;
constexpr float k_wallrunCornerSourceIgnoreTime = 0.16f;

/// @brief Check if a wall normal matches the blacklist (same wall).
/// @param normal    Wall normal to test.
/// @param velocity  Current entity velocity.
/// @param blNormal  Blacklisted wall normal.
/// @param active    Whether the blacklist is active.
/// @return True if the wall is blacklisted.
bool isBlacklisted(const glm::vec3& normal, glm::vec3 velocity, const glm::vec3& blNormal, bool active)
{
    if (!active)
        return false;
    if (glm::dot(normalizedOrZero(normal), normalizedOrZero(blNormal)) <= 0.9f)
        return false;

    const glm::vec3 horizontalVelocity = horizVel(velocity);
    const float inwardSpeed = std::max(0.0f, -glm::dot(horizontalVelocity, normalizedOrZero(blNormal)));
    return inwardSpeed < tms::k_wallrunSameWallReattachSpeed;
}

physics::CapsuleShape capsuleQueryForWallrun(const CollisionShape& shape)
{
    return {.radius = shape.radius, .halfHeight = shape.halfHeight, .up = glm::vec3{0.0f, 1.0f, 0.0f}};
}

struct WallAttachmentProbe
{
    bool found{false};
    glm::vec3 anchor{0.0f};
    glm::vec3 normal{0.0f};
    uint32_t meshIndex{UINT32_MAX};
    uint32_t triId{UINT32_MAX};
    physics::TriRegion region{physics::TriRegion::Face};
};

void clearWallCornerTransition(PlayerStateRef state)
{
    state.sim.wallCornerTransitionActive = false;
    state.sim.wallCornerAnchor = glm::vec3{0.0f};
    state.sim.wallCornerFromNormal = glm::vec3{0.0f};
    state.sim.wallCornerFromForward = glm::vec3{0.0f};
    state.sim.wallCornerToNormal = glm::vec3{0.0f};
    state.sim.wallCornerToForward = glm::vec3{0.0f};
    state.sim.wallCornerMeshIndex = UINT32_MAX;
    state.sim.wallCornerTriId = UINT32_MAX;
    state.sim.wallCornerRegion = physics::TriRegion::Face;
    state.sim.wallCornerTimer = 0.0f;
}

void clearWallCornerIgnore(PlayerStateRef state)
{
    state.sim.wallCornerIgnoreNormal = glm::vec3{0.0f};
    state.sim.wallCornerIgnoreTimer = 0.0f;
}

void clearWallrunBlocker(PlayerStateRef state)
{
    state.sim.wallrunBlockerActive = false;
    state.sim.wallrunBlockerNormal = glm::vec3{0.0f};
    state.sim.wallrunBlockerTimer = 0.0f;
    state.sim.wallrunBlockedFrames = 0;
}

void clearWallrunCollisionConstraints(PlayerStateRef state)
{
    clearWallrunBlocker(state);
    state.sim.wallrunCeilingConstrainedTimer = 0.0f;
}

glm::vec3 clipVelocityOutOfPlanes(glm::vec3 velocity, glm::vec3 normalA, glm::vec3 normalB)
{
    std::array<glm::vec3, 2> normals{normalizedOrZero(normalA), normalizedOrZero(normalB)};
    for (int pass = 0; pass < 3; ++pass) {
        bool clipped = false;
        for (glm::vec3 normal : normals) {
            if (!usableNormal(normal))
                continue;
            const float into = glm::dot(velocity, normal);
            if (into < 0.0f) {
                velocity -= normal * into;
                clipped = true;
            }
        }
        if (!clipped)
            break;
    }

    for (glm::vec3 normal : normals) {
        if (usableNormal(normal) && glm::dot(velocity, normal) < -0.01f)
            return glm::vec3{0.0f};
    }
    return velocity;
}

bool wallAndBlockerLeaveHorizontalSlide(glm::vec3 wallNormal, glm::vec3 blockerNormal)
{
    wallNormal = normalizedOrZero(wallNormal);
    blockerNormal = normalizedOrZero(blockerNormal);
    if (!usableNormal(wallNormal) || !usableNormal(blockerNormal))
        return true;
    if (std::abs(wallNormal.y) > 0.2f || std::abs(blockerNormal.y) > 0.2f)
        return true;
    if (std::abs(glm::dot(wallNormal, blockerNormal)) > 0.95f)
        return true;

    const glm::vec3 crease = glm::cross(wallNormal, blockerNormal);
    return glm::length(horizVel(crease)) > 0.1f;
}

bool blockerStillRelevant(const PlayerSimState& sim)
{
    if (!sim.wallrunBlockerActive || !usableNormal(sim.wallrunBlockerNormal))
        return false;

    return sim.wallrunBlockerTimer > 0.0f ||
           glm::dot(normalizedOrZero(sim.wallForward), normalizedOrZero(sim.wallrunBlockerNormal)) < -0.05f;
}

WallAttachmentProbe findWallAttachment(glm::vec3 pos,
                                       const CollisionShape& shape,
                                       const physics::WorldGeometry& world,
                                       glm::vec3 continuityNormal,
                                       glm::vec3 travelDir = glm::vec3{0.0f},
                                       float lookaheadDist = 0.0f,
                                       uint32_t previousMeshIndex = UINT32_MAX,
                                       uint32_t previousTriId = UINT32_MAX,
                                       physics::TriRegion previousRegion = physics::TriRegion::Face)
{
    WallAttachmentProbe best;
    if (shape.type != CollisionShapeType::Capsule)
        return best;

    const physics::WallAttachmentResult attachment = physics::findWallRunAttachment(capsuleQueryForWallrun(shape),
                                                                                    pos,
                                                                                    world,
                                                                                    continuityNormal,
                                                                                    travelDir,
                                                                                    lookaheadDist,
                                                                                    tms::k_wallrunCheckDist,
                                                                                    previousMeshIndex,
                                                                                    previousTriId,
                                                                                    previousRegion);
    if (!attachment.found)
        return best;

    best.found = true;
    best.anchor = attachment.anchor;
    best.normal = attachment.normal;
    best.meshIndex = attachment.meshIndex;
    best.triId = attachment.triId;
    best.region = attachment.region;

    return best;
}

void seedWallAttachmentFromProbe(PlayerStateRef state,
                                 const glm::vec3& point,
                                 const glm::vec3& normal,
                                 uint32_t meshIndex,
                                 uint32_t triId,
                                 physics::TriRegion region)
{
    state.sim.wallAnchor = point;
    state.sim.wallNormal = normal;
    state.sim.wallMeshIndex = meshIndex;
    state.sim.wallTriId = triId;
    state.sim.wallRegion = region;
    state.sim.wallAttachmentValid = true;
}

glm::vec3 redirectWallForward(glm::vec3 oldForward, glm::vec3 oldNormal, glm::vec3 newNormal)
{
    glm::vec3 redirected = oldForward + oldNormal;
    redirected -= newNormal * glm::dot(redirected, newNormal);
    redirected.y = 0.0f;

    if (glm::length(redirected) < 0.001f) {
        redirected = glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, newNormal);
        redirected.y = 0.0f;
        if (glm::dot(redirected, oldForward) < 0.0f)
            redirected = -redirected;
    }

    const float len = glm::length(redirected);
    return (len > 0.001f) ? redirected / len : oldForward;
}

bool isBoundaryFeature(physics::TriRegion region)
{
    return region != physics::TriRegion::Face;
}

bool isTrailingSameWallBoundary(const WallAttachmentProbe& attachment,
                                glm::vec3 pos,
                                glm::vec3 oldForward,
                                glm::vec3 oldNormal,
                                float normalTurn,
                                float radius)
{
    if (!isBoundaryFeature(attachment.region) || normalTurn >= 0.05f)
        return false;
    if (glm::dot(attachment.normal, oldNormal) < 0.98f)
        return false;

    const float alongTravel = glm::dot(attachment.anchor - pos, oldForward);
    return alongTravel < -std::max(radius * 0.25f, 1.0f);
}

bool isForwardCompatibleWallHandoff(glm::vec3 oldForward, glm::vec3 oldNormal, glm::vec3 newNormal, glm::vec3 velocity)
{
    const glm::vec3 redirected = redirectWallForward(oldForward, oldNormal, newNormal);
    if (glm::dot(redirected, oldForward) < -0.05f)
        return false;

    const glm::vec3 horizontalVelocity = horizVel(velocity);
    const float horizontalSpeed = glm::length(horizontalVelocity);
    if (horizontalSpeed > 1.0f && glm::dot(horizontalVelocity / horizontalSpeed, redirected) < -0.05f)
        return false;

    return true;
}

glm::vec3 projectedContinuationForward(glm::vec3 oldForward, glm::vec3 newNormal, glm::vec3 referenceVelocity)
{
    newNormal = normalizedOrZero(newNormal);
    oldForward = normalizedOrZero(oldForward);
    if (!usableNormal(newNormal))
        return oldForward;

    auto projectTangent = [&](glm::vec3 source) {
        source = horizVel(source);
        if (glm::length(source) < 1.0f)
            return glm::vec3{0.0f};

        glm::vec3 tangent = source - newNormal * glm::dot(source, newNormal);
        tangent.y = 0.0f;
        const float len = glm::length(tangent);
        if (len < 0.001f)
            return glm::vec3{0.0f};

        tangent /= len;
        if (usableNormal(oldForward) && glm::dot(tangent, oldForward) < 0.0f)
            tangent = -tangent;
        return tangent;
    };

    glm::vec3 tangent = projectTangent(referenceVelocity);
    if (usableNormal(tangent) && (!usableNormal(oldForward) || glm::dot(tangent, oldForward) >= -0.05f))
        return tangent;

    tangent = projectTangent(oldForward);
    if (usableNormal(tangent))
        return tangent;

    return redirectWallForward(oldForward, glm::vec3{0.0f}, newNormal);
}

bool isWallrunContinuationCandidate(glm::vec3 oldForward,
                                    glm::vec3 oldNormal,
                                    glm::vec3 newNormal,
                                    glm::vec3 referenceVelocity,
                                    const InputSnapshot& input)
{
    oldForward = normalizedOrZero(oldForward);
    oldNormal = normalizedOrZero(oldNormal);
    newNormal = normalizedOrZero(newNormal);
    if (!usableNormal(oldNormal) || !usableNormal(newNormal))
        return false;
    if (std::abs(newNormal.y) > 0.25f)
        return false;
    if (glm::dot(oldNormal, newNormal) <= -0.05f)
        return false;

    const glm::vec3 continuationForward = projectedContinuationForward(oldForward, newNormal, referenceVelocity);
    if (!usableNormal(continuationForward))
        return false;
    if (usableNormal(oldForward) && glm::dot(continuationForward, oldForward) < -0.05f)
        return false;

    const glm::vec3 horizontalVelocity = horizVel(referenceVelocity);
    const float horizontalSpeed = glm::length(horizontalVelocity);
    if (horizontalSpeed > 1.0f && glm::dot(horizontalVelocity / horizontalSpeed, continuationForward) < -0.25f)
        return false;

    const glm::vec3 wishDir = physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
    if (glm::length(wishDir) > 0.5f && glm::dot(wishDir, continuationForward) < -0.35f)
        return false;

    return true;
}

glm::vec3 redirectWallForwardTowardAnchor(
    glm::vec3 oldForward, glm::vec3 oldNormal, glm::vec3 newNormal, glm::vec3 pos, glm::vec3 anchor)
{
    glm::vec3 anchorTangent = anchor - pos;
    anchorTangent -= newNormal * glm::dot(anchorTangent, newNormal);
    anchorTangent.y = 0.0f;

    const float anchorLen = glm::length(anchorTangent);
    if (anchorLen > 0.25f) {
        anchorTangent /= anchorLen;
        if (glm::dot(anchorTangent, oldForward) >= -0.05f)
            return anchorTangent;
    }

    return redirectWallForward(oldForward, oldNormal, newNormal);
}

float desiredWallrunStandoff(const CollisionShape& shape, glm::vec3 normal);
void finishWallrunFrame(
    glm::vec3& vel, PlayerStateRef state, const InputSnapshot& input, float dt, float standoffCorrection);

bool applyWallrunContinuationContact(glm::vec3 pos,
                                     glm::vec3& vel,
                                     PlayerStateRef state,
                                     const InputSnapshot& input,
                                     const CollisionShape& shape,
                                     const physics::WorldGeometry& world,
                                     glm::vec3 contactNormal,
                                     glm::vec3 referenceVelocity,
                                     float dt)
{
    contactNormal = normalizedOrZero(contactNormal);
    if (!isWallrunContinuationCandidate(
            state.sim.wallForward, state.sim.wallNormal, contactNormal, referenceVelocity, input))
        return false;

    const glm::vec3 continuationForward =
        projectedContinuationForward(state.sim.wallForward, contactNormal, referenceVelocity);
    const float horizontalSpeed = glm::length(horizVel(referenceVelocity));
    const float handoffLookahead = std::clamp(horizontalSpeed * dt + 4.0f, 4.0f, shape.radius);
    WallAttachmentProbe attachment = findWallAttachment(pos,
                                                        shape,
                                                        world,
                                                        contactNormal,
                                                        continuationForward,
                                                        handoffLookahead,
                                                        UINT32_MAX,
                                                        UINT32_MAX,
                                                        physics::TriRegion::Face);
    if (!attachment.found || glm::dot(attachment.normal, contactNormal) < 0.75f)
        return false;

    const float verticalVel = vel.y;
    const float preservedSpeed = std::max(glm::length(horizVel(vel)), horizontalSpeed);
    state.sim.wallAnchor = attachment.anchor;
    state.sim.wallNormal = attachment.normal;
    state.sim.wallForward = projectedContinuationForward(state.sim.wallForward, attachment.normal, referenceVelocity);
    state.sim.wallMeshIndex = attachment.meshIndex;
    state.sim.wallTriId = attachment.triId;
    state.sim.wallRegion = attachment.region;
    state.sim.wallAttachmentValid = true;
    clearWallCornerTransition(state);
    clearWallrunBlocker(state);

    if (preservedSpeed > 1.0f) {
        vel = state.sim.wallForward * preservedSpeed;
        vel.y = verticalVel;
    }
    return true;
}

bool wallrunContactPreservesTraversal(glm::vec3 wallForward,
                                      glm::vec3 wallNormal,
                                      glm::vec3 contactNormal,
                                      glm::vec3 referenceVelocity,
                                      const InputSnapshot& input)
{
    wallForward = normalizedOrZero(wallForward);
    wallNormal = normalizedOrZero(wallNormal);
    contactNormal = normalizedOrZero(contactNormal);
    if (!usableNormal(wallForward) || !usableNormal(wallNormal) || !usableNormal(contactNormal))
        return false;
    if (!isWallrunContinuationCandidate(wallForward, wallNormal, contactNormal, referenceVelocity, input))
        return false;

    // Contacts behind or beside the intended tangent are seam constraints. A
    // front-facing contact still remains a blocker/endcap and must be handled
    // by the blocker path below.
    return glm::dot(wallForward, contactNormal) >= -0.05f;
}

glm::vec3 chooseWallrunTraversalConstraintNormal(const physics::KccFrameResult& kcc,
                                                 const PlayerSimState& sim,
                                                 const InputSnapshot& input)
{
    const glm::vec3 blockerNormal = normalizedOrZero(kcc.blockerNormal);
    const glm::vec3 wallForward = normalizedOrZero(sim.wallForward);
    if (kcc.hitBlocker && usableNormal(blockerNormal) && usableNormal(wallForward) &&
        glm::dot(wallForward, blockerNormal) < -0.05f)
    {
        return glm::vec3{0.0f};
    }

    const std::array<glm::vec3, 2> candidates{kcc.lastHitNormal, kcc.blockerNormal};
    for (glm::vec3 normal : candidates) {
        normal = normalizedOrZero(normal);
        if (wallrunContactPreservesTraversal(sim.wallForward, sim.wallNormal, normal, kcc.velBefore, input))
            return normal;
    }
    return glm::vec3{0.0f};
}

glm::vec3 restoreWallrunTraversalVelocity(glm::vec3 velocity,
                                          glm::vec3 referenceVelocity,
                                          glm::vec3 wallForward,
                                          glm::vec3 wallNormal,
                                          glm::vec3 contactNormal)
{
    wallForward = normalizedOrZero(wallForward);
    wallNormal = normalizedOrZero(wallNormal);
    contactNormal = normalizedOrZero(contactNormal);
    if (!usableNormal(wallForward))
        return velocity;

    const float currentSpeed = glm::length(horizVel(velocity));
    const float referenceSpeed = glm::length(horizVel(referenceVelocity));
    const float preservedSpeed = std::max(currentSpeed, referenceSpeed);
    if (preservedSpeed <= 1.0f)
        return velocity;

    const glm::vec3 currentDir = currentSpeed > 1.0f ? horizVel(velocity) / currentSpeed : glm::vec3{0.0f};
    if (currentSpeed < 1.0f || glm::dot(currentDir, wallForward) < 0.25f) {
        glm::vec3 glide = wallForward;
        glide = clipVelocityOutOfPlanes(glide, contactNormal, wallNormal);
        glide.y = 0.0f;
        const float glideLen = glm::length(glide);
        if (glideLen > 0.001f) {
            glide /= glideLen;
            if (glm::dot(glide, wallForward) < 0.0f)
                glide = -glide;
        } else {
            glide = wallForward;
        }

        velocity.x = glide.x * preservedSpeed;
        velocity.z = glide.z * preservedSpeed;
    }

    return clipVelocityOutOfPlanes(velocity, contactNormal, wallNormal);
}

void queueWallrunKccCorrection(PlayerStateRef state, glm::vec3 normal, float correction)
{
    if (!std::isfinite(correction) || !usableNormal(normal))
        return;

    state.sim.pendingKccCorrection += normalizedOrZero(normal) * correction;
}

void finishWallrunFrame(
    glm::vec3& vel, PlayerStateRef state, const InputSnapshot& input, float dt, float standoffCorrection = 0.0f)
{
    const glm::vec3 k_hv = horizVel(vel);
    const float k_hvLen = glm::length(k_hv);

    glm::vec3 wallFwd;
    if (k_hvLen > 1.0f) {
        wallFwd = k_hv - state.sim.wallNormal * glm::dot(k_hv, state.sim.wallNormal);
        const float k_projLen = glm::length(wallFwd);
        if (k_projLen > 0.001f)
            wallFwd /= k_projLen;
        else
            wallFwd = state.sim.wallForward;
    } else {
        wallFwd = state.sim.wallForward;
    }

    if (glm::dot(state.sim.wallForward, wallFwd) < 0.0f)
        wallFwd = -wallFwd;
    state.sim.wallForward = wallFwd;

    const float k_currentFwdSpeed = glm::dot(k_hv, state.sim.wallForward);
    if (k_currentFwdSpeed < tms::k_wallrunMaxSpeed) {
        const float k_addSpeed = std::min(tms::k_wallrunAccel * dt, tms::k_wallrunMaxSpeed - k_currentFwdSpeed);
        vel += state.sim.wallForward * k_addSpeed;
    }

    if (state.sim.wallRunSpeedTimer > tms::k_wallrunSpeedLossDelay)
        clampHorizSpeed(vel, tms::k_wallrunMaxSpeed);

    vel.y *= std::exp(-dt / tms::k_wallrunVerticalDecayTau);
    vel -= state.sim.wallNormal * glm::dot(vel, state.sim.wallNormal);
    if (blockerStillRelevant(state.sim)) {
        const float preClipHoriz = glm::length(horizVel(vel));
        vel = clipVelocityOutOfPlanes(vel, state.sim.wallrunBlockerNormal, state.sim.wallNormal);
        if (!wallAndBlockerLeaveHorizontalSlide(state.sim.wallNormal, state.sim.wallrunBlockerNormal) &&
            glm::dot(normalizedOrZero(state.sim.wallForward), normalizedOrZero(state.sim.wallrunBlockerNormal)) <
                -0.05f)
        {
            vel.x = 0.0f;
            vel.z = 0.0f;
        }
        if (preClipHoriz > 1.0f && glm::length(horizVel(vel)) < 1.0f)
            ++state.sim.wallrunBlockedFrames;
    }
    queueWallrunKccCorrection(state, state.sim.wallNormal, standoffCorrection);

    const float sideDot = glm::dot(state.sim.wallNormal, glm::vec3{std::cos(input.yaw), 0.0f, -std::sin(input.yaw)});
    state.vis.wallRunSide = (sideDot < 0.0f) ? WallSide::Right : WallSide::Left;
    state.vis.targetCameraTilt =
        std::clamp(-sideDot * tms::k_wallrunCameraTilt, -tms::k_wallrunCameraTilt, tms::k_wallrunCameraTilt);
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
                     const CollisionShape& shape,
                     const physics::WorldGeometry& world,
                     glm::vec3 pos)
{
    if (state.vis.moveMode != MoveMode::OnFoot)
        return;
    if (state.vis.grounded)
        return;
    if (glm::length(horizVel(vel)) < tms::k_wallrunMinAttachSpeed)
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
    auto tryWall = [&](bool hasWall,
                       const glm::vec3& wallNorm,
                       const glm::vec3& wallPoint,
                       uint32_t meshIndex,
                       uint32_t triId,
                       physics::TriRegion region,
                       WallSide side) {
        if (!hasWall)
            return false;
        if (isBlacklisted(wallNorm, vel, state.sim.wallBlacklistNormal, state.sim.wallBlacklistActive))
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
        clearWallCornerTransition(state);
        clearWallCornerIgnore(state);
        clearWallrunCollisionConstraints(state);
        seedWallAttachmentFromProbe(state, wallPoint, wallNorm, meshIndex, triId, region);

        const WallAttachmentProbe attachment = findWallAttachment(pos, shape, world, wallNorm);
        if (attachment.found) {
            state.sim.wallAnchor = attachment.anchor;
            state.sim.wallNormal = attachment.normal;
            state.sim.wallMeshIndex = attachment.meshIndex;
            state.sim.wallTriId = attachment.triId;
            state.sim.wallRegion = attachment.region;
            state.sim.wallAttachmentValid = true;

            wallFwd = glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, state.sim.wallNormal);
            if (side == WallSide::Left)
                wallFwd = -wallFwd;
            if (glm::length(k_hv) > 0.001f && glm::dot(glm::normalize(k_hv), wallFwd) < 0.0f)
                wallFwd = -wallFwd;
            state.sim.wallForward = glm::normalize(wallFwd);
        }

        // DJ no longer refreshes from entering wallrun — only from ground time.
        state.vis.jumpCount = 0;

        vel.x *= tms::k_wallrunEntryHorizSnap;
        vel.z *= tms::k_wallrunEntryHorizSnap;
        const float k_gravDir = state.vis.gravityFlipped ? -1.0f : 1.0f;
        vel.y = std::clamp(vel.y + tms::k_wallrunEntryVerticalImpulse * k_gravDir,
                           -tms::k_wallrunEntryVerticalCeiling,
                           tms::k_wallrunEntryVerticalCeiling);

        return true;
    };

    if (!tryWall(walls.wallRight,
                 walls.rightNormal,
                 walls.rightPoint,
                 walls.rightMeshIndex,
                 walls.rightTriId,
                 walls.rightRegion,
                 WallSide::Right))
        tryWall(walls.wallLeft,
                walls.leftNormal,
                walls.leftPoint,
                walls.leftMeshIndex,
                walls.leftTriId,
                walls.leftRegion,
                WallSide::Left);
}

/// @brief Exit wallrun mode while preserving same-wall reattach context.
/// @param state  Player state (modified in place).
/// @param posY   Current vertical position for blacklist height.
void exitWallrun(PlayerStateRef state, float posY)
{
    state.vis.moveMode = MoveMode::OnFoot;
    state.vis.exitingWall = false;
    state.sim.exitWallTimer = 0.0f;
    state.sim.wasWallRunning = true;
    state.sim.coyoteTimer = tms::k_coyoteTime;
    state.sim.wallBlacklistActive = true;
    state.sim.wallBlacklistNormal = state.sim.wallNormal;
    state.sim.wallBlacklistHeight = posY;
    state.sim.wallAttachmentValid = false;
    state.sim.wallMeshIndex = UINT32_MAX;
    state.sim.wallTriId = UINT32_MAX;
    clearWallCornerTransition(state);
    clearWallCornerIgnore(state);
    clearWallrunCollisionConstraints(state);
}

float desiredWallrunStandoff(const CollisionShape& shape, glm::vec3 normal)
{
    return (shape.type == CollisionShapeType::Capsule)
               ? capsuleQueryForWallrun(shape).minkowskiExtent(normal) + k_wallrunAttachPushback
               : shape.minkowskiExtent(normal) + k_wallrunAttachPushback;
}

void startWallCornerTransition(PlayerStateRef state,
                               const WallAttachmentProbe& attachment,
                               glm::vec3 fromNormal,
                               glm::vec3 fromForward,
                               glm::vec3 toForward)
{
    state.sim.wallCornerTransitionActive = true;
    state.sim.wallCornerAnchor = attachment.anchor;
    state.sim.wallCornerFromNormal = fromNormal;
    state.sim.wallCornerFromForward = fromForward;
    state.sim.wallCornerToNormal = attachment.normal;
    state.sim.wallCornerToForward = toForward;
    state.sim.wallCornerMeshIndex = attachment.meshIndex;
    state.sim.wallCornerTriId = attachment.triId;
    state.sim.wallCornerRegion = attachment.region;
    state.sim.wallCornerTimer = 0.0f;
}

bool handleWallCornerTransition(glm::vec3& pos,
                                glm::vec3& vel,
                                PlayerStateRef state,
                                const InputSnapshot& input,
                                const CollisionShape& shape,
                                const physics::WorldGeometry& world,
                                float posY,
                                float dt,
                                float preAttachHorizSpeed)
{
    if (!state.sim.wallCornerTransitionActive)
        return false;

    state.sim.wallCornerTimer += dt;

    const glm::vec3 fromNormal = state.sim.wallCornerFromNormal;
    const glm::vec3 fromForward = state.sim.wallCornerFromForward;
    const glm::vec3 cornerAnchor = state.sim.wallCornerAnchor;
    const float clearance = shape.radius + k_wallrunCornerClearancePadding;
    const float progress = glm::dot(pos - cornerAnchor, fromForward);
    const bool clearOfOldWall = progress >= clearance;

    if (!clearOfOldWall) {
        WallAttachmentProbe oldAttachment = findWallAttachment(pos,
                                                               shape,
                                                               world,
                                                               fromNormal,
                                                               glm::vec3{0.0f},
                                                               0.0f,
                                                               state.sim.wallMeshIndex,
                                                               state.sim.wallTriId,
                                                               state.sim.wallRegion);
        state.sim.wallNormal = fromNormal;
        state.sim.wallForward = fromForward;
        float standoffCorrection = 0.0f;
        const bool stillHoldingOldWall = oldAttachment.found && glm::dot(oldAttachment.normal, fromNormal) > 0.95f;
        if (stillHoldingOldWall) {
            state.sim.wallAnchor = oldAttachment.anchor;
            state.sim.wallMeshIndex = oldAttachment.meshIndex;
            state.sim.wallTriId = oldAttachment.triId;
            state.sim.wallRegion = oldAttachment.region;

            const float desiredStandoff = desiredWallrunStandoff(shape, state.sim.wallNormal);
            const float currentStandoff = glm::dot(pos - state.sim.wallAnchor, state.sim.wallNormal);
            standoffCorrection = desiredStandoff - currentStandoff;
        }

        if (state.sim.wallCornerTimer > k_wallrunCornerMaxTransitionTime) {
            exitWallrun(state, posY);
            return true;
        }

        const float verticalVel = vel.y;
        vel = fromForward * preAttachHorizSpeed;
        vel.y = verticalVel;
        finishWallrunFrame(vel, state, input, dt, standoffCorrection);
        return true;
    }

    glm::vec3 toNormal = state.sim.wallCornerToNormal;
    const glm::vec3 toAnchor = state.sim.wallCornerAnchor;
    if (glm::dot(pos - toAnchor, toNormal) < 0.0f)
        toNormal = -toNormal;

    const glm::vec3 toForward = redirectWallForwardTowardAnchor(fromForward, fromNormal, toNormal, pos, toAnchor);
    const glm::vec3 wishDir = physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
    if (glm::length(wishDir) > 0.5f && glm::dot(wishDir, toForward) < -0.05f) {
        exitWallrun(state, posY);
        return true;
    }

    state.sim.wallAnchor = toAnchor;
    state.sim.wallNormal = toNormal;
    state.sim.wallForward = toForward;
    state.sim.wallMeshIndex = state.sim.wallCornerMeshIndex;
    state.sim.wallTriId = state.sim.wallCornerTriId;
    state.sim.wallRegion = state.sim.wallCornerRegion;
    state.sim.wallAttachmentValid = true;
    clearWallCornerTransition(state);
    state.sim.wallCornerIgnoreNormal = fromNormal;
    state.sim.wallCornerIgnoreTimer = k_wallrunCornerSourceIgnoreTime;

    const float desiredStandoff = desiredWallrunStandoff(shape, state.sim.wallNormal);
    const float currentStandoff = glm::dot(pos - state.sim.wallAnchor, state.sim.wallNormal);
    const float standoffCorrection = desiredStandoff - currentStandoff;

    const float verticalVel = vel.y;
    vel = state.sim.wallForward * preAttachHorizSpeed;
    vel.y = verticalVel;
    finishWallrunFrame(vel, state, input, dt, standoffCorrection);
    return true;
}

/// @brief Process wallrunning movement, exit conditions, and camera tilt.
/// @param pos          Entity position (modified for curved-surface correction).
/// @param vel          Velocity (modified in place).
/// @param state        Player state (modified in place).
/// @param input        Current input snapshot.
/// @param walls        Wall detection result from this tick.
/// @param shape        Player collision shape used for wall standoff.
/// @param world        World collision geometry used to refresh attachment.
/// @param posY         Current vertical position of the entity.
/// @param dt           Fixed physics delta time in seconds.
void handleWallRunning(glm::vec3& pos,
                       glm::vec3& vel,
                       PlayerStateRef state,
                       const InputSnapshot& input,
                       const physics::WallDetectionResult& walls,
                       const CollisionShape& shape,
                       const physics::WorldGeometry& world,
                       float posY,
                       float dt)
{
    state.sim.wallRunTimer += dt;
    state.sim.wallRunSpeedTimer += dt;
    state.sim.wallCornerIgnoreTimer = std::max(0.0f, state.sim.wallCornerIgnoreTimer - dt);
    state.sim.wallrunBlockerTimer = std::max(0.0f, state.sim.wallrunBlockerTimer - dt);
    state.sim.wallrunCeilingConstrainedTimer = std::max(0.0f, state.sim.wallrunCeilingConstrainedTimer - dt);
    if (state.sim.wallrunBlockerActive && !blockerStillRelevant(state.sim))
        clearWallrunBlocker(state);

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

    // No look-away detach: Lucio glide stays attached regardless of where the
    // player looks. The only ways out are jump release (above) or losing wall
    // contact (e.g. the wall ends).

    const glm::vec3 oldNormal = state.sim.wallNormal;
    const glm::vec3 oldForward = state.sim.wallForward;
    const float preAttachHorizSpeed = glm::length(horizVel(vel));
    const bool collisionHeldWallrun = state.sim.wallrunBlockerActive || state.sim.wallrunCeilingConstrainedTimer > 0.0f;
    if (preAttachHorizSpeed < tms::k_wallrunMinStaySpeed && !collisionHeldWallrun) {
        exitWallrun(state, posY);
        return;
    }
    const float handoffLookahead = std::clamp(preAttachHorizSpeed * dt + 4.0f, 4.0f, shape.radius);

    if (handleWallCornerTransition(pos, vel, state, input, shape, world, posY, dt, preAttachHorizSpeed))
        return;

    WallAttachmentProbe attachment = findWallAttachment(pos,
                                                        shape,
                                                        world,
                                                        oldNormal,
                                                        oldForward,
                                                        handoffLookahead,
                                                        state.sim.wallMeshIndex,
                                                        state.sim.wallTriId,
                                                        state.sim.wallRegion);
    if (!attachment.found) {
        float bestFallbackContinuity = -1.0f;
        auto considerFallback = [&](bool found,
                                    glm::vec3 point,
                                    glm::vec3 normal,
                                    uint32_t meshIndex,
                                    uint32_t triId,
                                    physics::TriRegion region) {
            if (!found)
                return;

            normal = normalizedOrZero(normal);
            const float continuity = glm::dot(normal, oldNormal);
            if (continuity <= -0.05f || continuity <= bestFallbackContinuity)
                return;

            bestFallbackContinuity = continuity;
            attachment.found = true;
            attachment.anchor = point;
            attachment.normal = normal;
            attachment.meshIndex = meshIndex;
            attachment.triId = triId;
            attachment.region = region;
        };

        considerFallback(walls.wallRight,
                         walls.rightPoint,
                         walls.rightNormal,
                         walls.rightMeshIndex,
                         walls.rightTriId,
                         walls.rightRegion);
        considerFallback(
            walls.wallLeft, walls.leftPoint, walls.leftNormal, walls.leftMeshIndex, walls.leftTriId, walls.leftRegion);
        if (isWallrunContinuationCandidate(oldForward, oldNormal, walls.frontNormal, vel, input))
            considerFallback(walls.wallFront,
                             walls.frontPoint,
                             walls.frontNormal,
                             walls.frontMeshIndex,
                             walls.frontTriId,
                             walls.frontRegion);

        if (!attachment.found) {
            exitWallrun(state, posY);
            return;
        }
    }

    if (attachment.found && state.sim.wallCornerIgnoreTimer > 0.0f &&
        glm::dot(attachment.normal, state.sim.wallCornerIgnoreNormal) > 0.95f)
    {
        const WallAttachmentProbe currentWallAttachment = findWallAttachment(pos,
                                                                             shape,
                                                                             world,
                                                                             oldNormal,
                                                                             glm::vec3{0.0f},
                                                                             0.0f,
                                                                             state.sim.wallMeshIndex,
                                                                             state.sim.wallTriId,
                                                                             state.sim.wallRegion);
        if (currentWallAttachment.found && glm::dot(currentWallAttachment.normal, oldNormal) > 0.95f) {
            attachment = currentWallAttachment;
        } else {
            WallAttachmentProbe detectedCurrentWall;
            auto considerDetectedCurrentWall = [&](bool found,
                                                   glm::vec3 point,
                                                   glm::vec3 normal,
                                                   uint32_t meshIndex,
                                                   uint32_t triId,
                                                   physics::TriRegion region) {
                if (detectedCurrentWall.found || !found)
                    return;
                normal = normalizedOrZero(normal);
                if (glm::dot(normal, oldNormal) <= 0.95f)
                    return;

                detectedCurrentWall.found = true;
                detectedCurrentWall.anchor = point;
                detectedCurrentWall.normal = normal;
                detectedCurrentWall.meshIndex = meshIndex;
                detectedCurrentWall.triId = triId;
                detectedCurrentWall.region = region;
            };

            considerDetectedCurrentWall(walls.wallRight,
                                        walls.rightPoint,
                                        walls.rightNormal,
                                        walls.rightMeshIndex,
                                        walls.rightTriId,
                                        walls.rightRegion);
            considerDetectedCurrentWall(walls.wallLeft,
                                        walls.leftPoint,
                                        walls.leftNormal,
                                        walls.leftMeshIndex,
                                        walls.leftTriId,
                                        walls.leftRegion);
            considerDetectedCurrentWall(walls.wallFront,
                                        walls.frontPoint,
                                        walls.frontNormal,
                                        walls.frontMeshIndex,
                                        walls.frontTriId,
                                        walls.frontRegion);

            if (!detectedCurrentWall.found) {
                exitWallrun(state, posY);
                return;
            }
            attachment = detectedCurrentWall;
        }
    }

    float normalTurn = std::acos(std::clamp(glm::dot(oldNormal, attachment.normal), -1.0f, 1.0f));
    if (normalTurn > tms::k_wallrunMaxFaceRedirect + 1e-4f) {
        exitWallrun(state, posY);
        return;
    }
    if (isTrailingSameWallBoundary(attachment, pos, oldForward, oldNormal, normalTurn, shape.radius)) {
        exitWallrun(state, posY);
        return;
    }
    if (normalTurn > 0.05f && !isForwardCompatibleWallHandoff(oldForward, oldNormal, attachment.normal, vel)) {
        exitWallrun(state, posY);
        return;
    }

    if (normalTurn > 0.05f) {
        const glm::vec3 redirectedForward =
            redirectWallForwardTowardAnchor(oldForward, oldNormal, attachment.normal, pos, attachment.anchor);
        if (glm::dot(redirectedForward, oldNormal) < k_wallrunCornerIntoOldWallDot) {
            startWallCornerTransition(state, attachment, oldNormal, oldForward, redirectedForward);
            const float verticalVel = vel.y;
            vel = oldForward * preAttachHorizSpeed;
            vel.y = verticalVel;
            finishWallrunFrame(vel, state, input, dt);
            return;
        }
    }

    state.sim.wallAnchor = attachment.anchor;
    state.sim.wallNormal = attachment.normal;
    state.sim.wallMeshIndex = attachment.meshIndex;
    state.sim.wallTriId = attachment.triId;
    state.sim.wallRegion = attachment.region;
    state.sim.wallAttachmentValid = true;

    const float desiredStandoff = desiredWallrunStandoff(shape, state.sim.wallNormal);
    const glm::vec3 preStandoffPos = pos;
    const float currentStandoff = glm::dot(pos - state.sim.wallAnchor, state.sim.wallNormal);
    const float standoffCorrection = desiredStandoff - currentStandoff;

    if (normalTurn > 0.05f) {
        const glm::vec3 redirectedForward = redirectWallForwardTowardAnchor(
            oldForward, oldNormal, state.sim.wallNormal, preStandoffPos, state.sim.wallAnchor);
        const glm::vec3 wishDir =
            physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
        if (glm::length(wishDir) > 0.5f && glm::dot(wishDir, redirectedForward) < -0.05f) {
            exitWallrun(state, posY);
            return;
        }

        state.sim.wallForward = redirectedForward;
        const float verticalVel = vel.y;
        vel = state.sim.wallForward * preAttachHorizSpeed;
        vel.y = verticalVel;
    }

    finishWallrunFrame(vel, state, input, dt, standoffCorrection);
}

} // namespace

void reconcileMovementAfterKcc(glm::vec3& pos,
                               glm::vec3& vel,
                               const CollisionShape& shape,
                               PlayerVisState& vis,
                               PlayerSimState& sim,
                               const InputSnapshot& input,
                               const physics::WorldGeometry& world,
                               const physics::KccFrameResult& kcc,
                               float dt)
{
    sim.lastKccResult = kcc;
    sim.hasLastKccResult = true;

    PlayerStateRef state{vis, sim};
    if (vis.moveMode != MoveMode::WallRunning) {
        if (sim.wallrunBlockerActive && sim.wallrunBlockerTimer <= 0.0f)
            clearWallrunBlocker(state);
        sim.wallrunCeilingConstrainedTimer = std::max(0.0f, sim.wallrunCeilingConstrainedTimer - dt);
        return;
    }

    if (kcc.hitCeiling) {
        sim.wallrunCeilingConstrainedTimer = 0.16f;
        if (vel.y > 0.0f)
            vel.y = 0.0f;
    }

    glm::vec3 contactContinuationNormal = kcc.blockerNormal;
    if (!usableNormal(contactContinuationNormal))
        contactContinuationNormal = kcc.lastHitNormal;
    if ((kcc.hitWall || kcc.hitBlocker) && usableNormal(contactContinuationNormal) &&
        applyWallrunContinuationContact(
            pos, vel, state, input, shape, world, contactContinuationNormal, kcc.velBefore, dt))
    {
        return;
    }

    const glm::vec3 traversalConstraintNormal = chooseWallrunTraversalConstraintNormal(kcc, sim, input);
    if ((kcc.hitWall || kcc.hitBlocker) && usableNormal(traversalConstraintNormal)) {
        WallAttachmentProbe currentWall =
            findWallAttachment(pos,
                               shape,
                               world,
                               sim.wallNormal,
                               sim.wallForward,
                               std::clamp(glm::length(horizVel(kcc.velBefore)) * dt, 0.0f, shape.radius),
                               sim.wallMeshIndex,
                               sim.wallTriId,
                               sim.wallRegion);
        if (currentWall.found && glm::dot(currentWall.normal, sim.wallNormal) > 0.75f) {
            sim.wallAnchor = currentWall.anchor;
            sim.wallNormal = currentWall.normal;
            sim.wallMeshIndex = currentWall.meshIndex;
            sim.wallTriId = currentWall.triId;
            sim.wallRegion = currentWall.region;
            sim.wallAttachmentValid = true;
        }

        clearWallrunBlocker(state);
        vel = restoreWallrunTraversalVelocity(
            vel, kcc.velBefore, sim.wallForward, sim.wallNormal, traversalConstraintNormal);
        return;
    }

    const bool severeProgressLoss = kcc.hitWall && kcc.progressRatio < 0.50f && glm::length(kcc.attemptedDelta) > 1.0f;
    const bool blocked = kcc.hitBlocker || kcc.caExhausted || kcc.resolvedOscillation || severeProgressLoss;
    if (!blocked)
        return;

    const glm::vec3 oldWallNormal = sim.wallNormal;
    glm::vec3 blockerNormal = kcc.blockerNormal;
    if (!usableNormal(blockerNormal))
        blockerNormal = kcc.lastHitNormal;
    if (!usableNormal(blockerNormal))
        return;
    blockerNormal = normalizedOrZero(blockerNormal);

    const glm::vec3 attemptedHoriz = horizVel(kcc.attemptedDelta);
    const float attemptedHorizLen = glm::length(attemptedHoriz);
    if ((kcc.caExhausted || kcc.resolvedOscillation || severeProgressLoss) && attemptedHorizLen > 0.25f) {
        const glm::vec3 attemptedDir = attemptedHoriz / attemptedHorizLen;
        const bool normalLooksLikeCurrentWall = glm::dot(blockerNormal, normalizedOrZero(oldWallNormal)) > 0.90f;
        const bool normalDoesNotStopTravel = glm::dot(blockerNormal, attemptedDir) > -0.25f;
        if (normalLooksLikeCurrentWall || normalDoesNotStopTravel)
            blockerNormal = -attemptedDir;
    }

    WallAttachmentProbe currentWall = findWallAttachment(
        pos, shape, world, oldWallNormal, glm::vec3{0.0f}, 0.0f, sim.wallMeshIndex, sim.wallTriId, sim.wallRegion);
    const bool stillHasWall = currentWall.found && glm::dot(currentWall.normal, oldWallNormal) > 0.9f;
    if (stillHasWall) {
        sim.wallAnchor = currentWall.anchor;
        sim.wallNormal = currentWall.normal;
        sim.wallMeshIndex = currentWall.meshIndex;
        sim.wallTriId = currentWall.triId;
        sim.wallRegion = currentWall.region;
        sim.wallAttachmentValid = true;
    } else if (!sim.wallAttachmentValid) {
        exitWallrun(state, pos.y);
        return;
    }

    sim.wallrunBlockerActive = true;
    sim.wallrunBlockerNormal = blockerNormal;
    sim.wallrunBlockerTimer = 0.18f;
    ++sim.wallrunBlockedFrames;

    if (kcc.caExhausted || kcc.resolvedOscillation || kcc.progressRatio < 0.25f) {
        const glm::vec3 resolvedVel = dt > 0.0f ? kcc.actualDelta / dt : glm::vec3{0.0f};
        vel.x = resolvedVel.x;
        vel.z = resolvedVel.z;
    }

    vel = clipVelocityOutOfPlanes(vel, sim.wallrunBlockerNormal, sim.wallNormal);

    glm::vec3 horizontal = horizVel(vel);
    if (glm::length(horizontal) < 1.0f || glm::dot(normalizedOrZero(horizontal), sim.wallrunBlockerNormal) < -0.01f ||
        !wallAndBlockerLeaveHorizontalSlide(sim.wallNormal, sim.wallrunBlockerNormal))
    {
        vel.x = 0.0f;
        vel.z = 0.0f;
    }
}

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
    physics::perf::add(&physics::perf::FrameStats::movementCalls);
    physics::perf::add(&physics::perf::FrameStats::movementPlayers, static_cast<std::uint32_t>(moveWork.size()));

    auto moveKernel = [&registry, dt, &world](entt::entity e) {
        auto& pos = registry.get<Position>(e);
        auto& vel = registry.get<Velocity>(e);
        auto& vis = registry.get<PlayerVisState>(e);
        auto& sim = registry.get<PlayerSimState>(e);
        auto& shape = registry.get<CollisionShape>(e);
        const auto& input = registry.get<InputSnapshot>(e);
        sim.pendingKccCorrection = glm::vec3{0.0f};
        {
            // Bundle the two halves into a single ref so the helper functions
            // below don't need a "(vis, sim)" pair on every call.
            PlayerStateRef state{vis, sim};
            const bool diagOn = physics::diag::isEnabled();
            physics::diag::MovementFrame movementDiag{};
            if (diagOn) {
                movementDiag.entity = e;
                movementDiag.posBefore = pos.value;
                movementDiag.velBefore = vel.value;
                movementDiag.modeBefore = static_cast<int>(state.vis.moveMode);
                movementDiag.groundedBefore = state.vis.grounded;
                movementDiag.inputForward = input.forward;
                movementDiag.inputBack = input.back;
                movementDiag.inputLeft = input.left;
                movementDiag.inputRight = input.right;
                movementDiag.inputJump = input.jump;
                movementDiag.inputCrouch = input.crouch;
                movementDiag.inputGrapple = input.grapple;
                movementDiag.yaw = input.yaw;
                movementDiag.pitch = input.pitch;
            }

            // 0. Tick timers
            tickTimers(state, dt);

            // Gravity direction multiplier: +1 normal, -1 flipped.
            const float gravDir = state.vis.gravityFlipped ? -1.0f : 1.0f;

            // 1. Wall detection
            // Pass the previous wall normal so the detector can trace toward
            // curved surfaces (cylinders, concave walls) whose normal rotates
            // as the player moves along them.
            physics::WallDetectionResult walls{};
            if (!state.vis.grounded || state.vis.moveMode == MoveMode::WallRunning) {
                const bool alreadyWallrunning = state.vis.moveMode == MoveMode::WallRunning;
                bool shouldProbeWalls = alreadyWallrunning;
                bool groundDistanceKnown = false;
                float knownGroundDistance = walls.groundDistance;

                if (!shouldProbeWalls && state.vis.moveMode == MoveMode::OnFoot && !state.vis.grounded && input.jump &&
                    glm::length(horizVel(vel.value)) >= tms::k_wallrunMinAttachSpeed)
                {
                    walls.groundDistance = physics::probeWallrunGroundDistance(
                        pos.value, shape.halfExtents, world, tms::k_wallrunSphereRadius, state.vis.gravityFlipped);
                    knownGroundDistance = walls.groundDistance;
                    groundDistanceKnown = true;
                    shouldProbeWalls = walls.groundDistance >= tms::k_wallrunMinGroundDist;
                }

                if (shouldProbeWalls) {
                    const glm::vec3 prevNormal = alreadyWallrunning ? state.sim.wallNormal : glm::vec3(0.0f);
                    walls = physics::detectWalls(pos.value,
                                                 input.yaw,
                                                 shape.halfExtents,
                                                 world,
                                                 tms::k_wallrunCheckDist,
                                                 tms::k_wallrunSphereRadius,
                                                 prevNormal,
                                                 state.vis.gravityFlipped,
                                                 !groundDistanceKnown);
                    if (groundDistanceKnown)
                        walls.groundDistance = knownGroundDistance;
                } else {
                    physics::perf::add(&physics::perf::FrameStats::wallDetectSkippedByGate);
                }
            }
            if (diagOn) {
                movementDiag.wallFront = walls.wallFront;
                movementDiag.groundDistance = walls.groundDistance;
                movementDiag.frontNormal = walls.frontNormal;
                movementDiag.frontPoint = walls.frontPoint;
            }

            // 2. Sprint update
            updateSprint(state, input);

            // 2b. ADS stance — RMB held while a charge (precision) weapon is
            // equipped caps wish speed via currentWishSpeed(). Derived here so
            // both client prediction and server agree (both have InputSnapshot
            // + WeaponState). Resets to false otherwise (no weapon, non-charge
            // gun, or RMB released).
            state.vis.ads = false;
            if (input.scoped) {
                if (const auto* weapon = registry.try_get<WeaponState>(e)) {
                    if (getWeaponConfig(getEquippedGun(*weapon).type).isCharge)
                        state.vis.ads = true;
                }
            }

            // 3. State transitions (try enter new modes)
            // Order matters: wallrun > slide
            if (state.vis.moveMode == MoveMode::OnFoot)
                tryEnterWallrun(vel.value, state, input, walls, shape, world, pos.value);
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
                    vel.value = physics::applyPlayerGravity(vel.value, dt, state.vis.gravityFlipped);
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
                handleSliding(vel.value, state, input, dt);
                // Slide camera lean: tilt based on lateral velocity relative to look direction.
                const float k_sinY = std::sin(input.yaw);
                const float k_cosY = std::cos(input.yaw);
                const glm::vec3 k_lookRight{k_cosY, 0.0f, -k_sinY};
                const float k_lateralSpeed = glm::dot(horizVel(vel.value), k_lookRight);
                state.vis.targetCameraTilt = std::clamp(k_lateralSpeed / 400.0f * 3.0f, -5.0f, 5.0f);
                break;
            }

            case MoveMode::WallRunning:
                handleWallRunning(pos.value, vel.value, state, input, walls, shape, world, pos.value.y, dt);
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
                    // Cancel wallrun/slide on grapple.
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
                // jumps and slidehops deliberately
                // don't restore it.
                if (state.sim.groundedDuration >= tms::k_doubleJumpGroundedRefreshTime)
                    state.sim.canDoubleJump = true;

                // Clear blacklists on landing.
                state.sim.wallBlacklistActive = false;
            }

            // 10. Auto-uncrouch / pending uncrouch
            // If the player should uncrouch (slidehop exit, or crouch key
            // released while crouched), validate that the standing capsule
            // fits at the current foot position BEFORE committing the
            // shape change.  This prevents the "stand up into the ceiling
            // and get depen-pushed back through the floor" failure mode
            // that the old try-then-revert approach was vulnerable to
            // when ceiling clearance was just below the resize threshold.
            if (state.vis.pendingUncrouch ||
                (state.vis.crouching && !input.crouch && state.vis.moveMode == MoveMode::OnFoot))
            {
                if (playerFitsAt(pos, shape, tms::k_standingHalfHeight, world)) {
                    state.vis.crouching = false;
                    resizePlayerCapsule(pos, shape, tms::k_standingHalfHeight);
                    state.vis.pendingUncrouch = false;
                } else if (!input.crouch) {
                    // Keep `pendingUncrouch` set so we retry next tick
                    // (player held duck momentarily then released, but
                    // there's still a low ceiling).  Pending stays true.
                    state.vis.pendingUncrouch = true;
                }
            }

            // 11. Speed cap
            applySpeedCap(vel.value, state);

            // 12. Track jump key state for edge detection
            state.sim.jumpHeldLastTick = input.jump;

            if (diagOn) {
                movementDiag.posAfter = pos.value;
                movementDiag.velAfter = vel.value;
                movementDiag.modeAfter = static_cast<int>(state.vis.moveMode);
                movementDiag.groundedAfter = state.vis.grounded;
                if (state.vis.grounded)
                    movementDiag.flags |= physics::diag::PhaseFlag::Grounded;
                if (state.vis.grappleActive)
                    movementDiag.flags |= physics::diag::PhaseFlag::GrappleActive;
                if (state.vis.gravityFlipped)
                    movementDiag.flags |= physics::diag::PhaseFlag::GravityFlipped;
                if (state.vis.moveMode == MoveMode::WallRunning)
                    movementDiag.flags |= physics::diag::PhaseFlag::WallRunning;
                if (state.vis.moveMode == MoveMode::Sliding)
                    movementDiag.flags |= physics::diag::PhaseFlag::Sliding;
                if (state.sim.jumpedThisTick)
                    movementDiag.flags |= physics::diag::PhaseFlag::DoubleJumped;
                if (state.sim.wallrunBlockerActive)
                    movementDiag.flags |= physics::diag::PhaseFlag::WallrunBlocked;
                if (state.sim.wallrunCeilingConstrainedTimer > 0.0f)
                    movementDiag.flags |= physics::diag::PhaseFlag::WallrunCeilingConstrained;
                if (state.sim.hasLastKccResult && state.sim.lastKccResult.resolvedOscillation)
                    movementDiag.flags |= physics::diag::PhaseFlag::KccOscillationResolved;
                physics::diag::recordMovementFrame(movementDiag);
            }
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
