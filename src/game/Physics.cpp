#include "Physics.hpp"

#include "../ecs/Components.hpp"
#include "Weapons.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace
{

// Project v onto plane whose normal is n (remove the component along n)
glm::vec3 projectOnPlane(glm::vec3 v, glm::vec3 n)
{
    return v - n * glm::dot(v, n);
}

// Clamp total speed to maxVel while preserving direction
void clampSpeed(glm::vec3& vel, float maxVel)
{
    float spd = glm::length(vel);
    if (spd > maxVel)
        vel *= maxVel / spd;
}

// ---------------------------------------------------------------------------
// Source-engine Accelerate() — the heart of bhop and air-strafing.
// ---------------------------------------------------------------------------
void accelerate(glm::vec3& vel, glm::vec3 wishDir, float wishSpeed, float accel, float dt)
{
    float currentSpeed = glm::dot(vel, wishDir);
    float addSpeed = wishSpeed - currentSpeed;
    if (addSpeed <= 0.0f)
        return;
    float accelAmount = std::min(accel * wishSpeed * dt, addSpeed);
    vel += accelAmount * wishDir;
}

// ---------------------------------------------------------------------------
// Ground friction — Quake style
// ---------------------------------------------------------------------------
void applyFriction(glm::vec3& vel, glm::vec3 up, float stopSpeed, float friction, float dt)
{
    glm::vec3 lateralVel = projectOnPlane(vel, up);
    float speed = glm::length(lateralVel);
    if (speed < 0.001f) {
        vel -= lateralVel;
        return;
    }
    float control = std::max(speed, stopSpeed);
    float drop = control * friction * dt;
    float newSpeed = std::max(speed - drop, 0.0f);
    vel -= lateralVel * (1.0f - newSpeed / speed);
}

// ---------------------------------------------------------------------------
// AABB overlap test
// ---------------------------------------------------------------------------
bool aabbOverlap(glm::vec3 aCenter, glm::vec3 aHalf, glm::vec3 bCenter, glm::vec3 bHalf, glm::vec3& outDepth)
{
    glm::vec3 d = aCenter - bCenter;
    glm::vec3 sum = aHalf + bHalf;
    glm::vec3 overlap = sum - glm::abs(d);

    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f)
        return false;
    outDepth = overlap;
    return true;
}

// ---------------------------------------------------------------------------
// Resolve AABB vs world collisions — one substep pass.
//
// Minimum-penetration-axis (SAT) + 16 substeps per frame.
//
// Why min-penetration (NOT decomposed-axis):
//   The floor is half=(2000,20,2000). When gravity dips the player 0.01 qu
//   into the floor, overlap = (2032, 0.01, 2032). Min axis = Y (0.01) →
//   push up 0.01. Correct. A decomposed X-first pass would see overlap.x=2032
//   and shove the player 2032 units sideways into the perimeter wall — that
//   was the spawn-outside-map bug.
//
// Why substeps prevent tall-wall Y-sink:
//   At 16 substeps the max horizontal penetration per substep is ~7 qu.
//   For any tall box (e.g. tower half.y=300), overlap.y = 72 qu while
//   overlap.x = 7 qu → min = X → push back. No downward ejection.
// ---------------------------------------------------------------------------
glm::vec3 resolveCollisions(glm::vec3 pos, glm::vec3 half, const World& world, Velocity& vel, PlayerController& ctrl)
{
    constexpr float k_groundDot = 0.70f;
    constexpr int k_iters = 3;

    ctrl.onWall = false;
    ctrl.onGround = false;
    ctrl.onSurf = false;

    glm::vec3 result = pos;

    for (int iter = 0; iter < k_iters; ++iter) {
        for (const auto& box : world.boxes) {
            glm::vec3 depth;
            if (!aabbOverlap(result, half, box.center, box.half, depth))
                continue;

            // Minimum-penetration axis.
            int axis = 0;
            if (depth[1] < depth[0])
                axis = 1;
            if (depth[2] < depth[axis])
                axis = 2;

            float pen = (result[axis] > box.center[axis]) ? depth[axis] : -depth[axis];
            result[axis] += pen;

            glm::vec3 normal{0.0f};
            normal[axis] = (pen > 0.0f) ? 1.0f : -1.0f;

            float upDot = glm::dot(normal, glm::vec3(0, 1, 0));

            if (upDot >= k_groundDot) {
                ctrl.onGround = true;
                ctrl.groundNormal = normal;
                if (vel.linear.y < 0.0f)
                    vel.linear.y = 0.0f;
            } else if (upDot <= -k_groundDot) {
                if (vel.linear.y > 0.0f)
                    vel.linear.y = 0.0f;
            } else {
                if (!ctrl.onWall) {
                    ctrl.onWall = true;
                    ctrl.wallNormal = normal;
                }
                float vDotN = glm::dot(vel.linear, normal);
                if (vDotN < 0.0f)
                    vel.linear -= normal * vDotN;
            }
        }
    }

    constexpr float k_killFloor = -200.0f;
    if (result.y < k_killFloor) {
        result.y = 36.0f;
        vel.linear = {0.0f, 0.0f, 0.0f};
        ctrl.onGround = true;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Wall-run helper: detect nearby vertical walls via short raycasts
// Returns true and fills wallNormal if a runnable wall is found.
// ---------------------------------------------------------------------------
bool detectWallRun(
    const glm::vec3& pos, const glm::vec3& half, const World& world, const glm::vec3& fwd, glm::vec3& outNormal)
{
    constexpr float k_wallSense = 8.0f;
    const float senseRange = half.x + k_wallSense;

    glm::vec3 right = glm::vec3(fwd.z, 0.0f, -fwd.x);
    glm::vec3 dirs[] = {right, -right};

    for (auto& dir : dirs) {
        for (const auto& box : world.boxes) {
            float tHit = 0.0f;
            glm::vec3 bMin = box.center - box.half - glm::vec3(1.0f);
            glm::vec3 bMax = box.center + box.half + glm::vec3(1.0f);
            if (rayVsAabb(pos, dir, senseRange, bMin, bMax, tHit)) {
                glm::vec3 normal = -dir;
                float upDot = std::abs(glm::dot(normal, glm::vec3(0, 1, 0)));
                if (upDot < 0.25f) {
                    outNormal = glm::normalize(normal - glm::vec3(0, normal.y, 0));
                    return true;
                }
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Grapple hook physics
// ---------------------------------------------------------------------------
void updateGrappleHook(GrappleHook& gh,
                       glm::vec3& pos,
                       Velocity& vel,
                       const PlayerController& ctrl,
                       const glm::vec3& eyePos,
                       const glm::vec3& aimDir,
                       bool fireHeld,
                       bool firePressed,
                       const World& world,
                       float dt)
{
    (void)ctrl;

    if (!fireHeld && gh.state != GrappleHook::State::Idle) {
        gh.state = GrappleHook::State::Idle;
        return;
    }

    switch (gh.state) {
    case GrappleHook::State::Idle:
        if (firePressed) {
            gh.state = GrappleHook::State::Flying;
            gh.tipPos = eyePos;
            gh.tipVel = aimDir * GrappleHook::k_flySpeed;
        }
        break;

    case GrappleHook::State::Flying: {
        gh.tipPos += gh.tipVel * dt;

        for (const auto& box : world.boxes) {
            float tHit = 0.0f;
            glm::vec3 bMin = box.center - box.half;
            glm::vec3 bMax = box.center + box.half;
            glm::vec3 stepDir = glm::normalize(gh.tipVel);
            float stepLen = glm::length(gh.tipVel) * dt;
            if (rayVsAabb(gh.tipPos - stepDir * stepLen, stepDir, stepLen + 2.0f, bMin, bMax, tHit)) {
                gh.state = GrappleHook::State::Attached;
                gh.anchorPoint = gh.tipPos;
                gh.ropeLength = glm::length(gh.anchorPoint - eyePos);
                break;
            }
        }

        if (glm::length(gh.tipPos - eyePos) > GrappleHook::k_maxRange)
            gh.state = GrappleHook::State::Idle;
        break;
    }

    case GrappleHook::State::Attached: {
        glm::vec3 toAnchor = gh.anchorPoint - eyePos;
        float dist = glm::length(toAnchor);
        if (dist > 1.0f) {
            glm::vec3 pullDir = toAnchor / dist;
            if (dist > gh.ropeLength * 0.95f)
                vel.linear += pullDir * GrappleHook::k_pullForce * dt;
        }
        gh.tipPos = gh.anchorPoint;
        (void)pos;
        break;
    }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// physicsUpdate — called every game tick
// ---------------------------------------------------------------------------

void physicsUpdate(entt::registry& reg, const World& world, const PhysicsConfig& cfg, float dt)
{
    auto view = reg.view<Transform, Velocity, PlayerController, InputState, CameraAngles>();

    for (auto entity : view) {
        auto& tf = view.get<Transform>(entity);
        auto& vel = view.get<Velocity>(entity);
        auto& ctrl = view.get<PlayerController>(entity);
        auto& inp = view.get<InputState>(entity);
        auto& cam = view.get<CameraAngles>(entity);

        const glm::vec3 up = -ctrl.gravityDir;
        const float gravity = ctrl.gravityMag;

        bool wasOnGround = ctrl.onGround;

        // ---- 1. Build wish direction ----------------------------------------
        float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
        glm::vec3 fwd = {-sy, 0.0f, cy};
        glm::vec3 right = {-cy, 0.0f, -sy};
        glm::vec3 wishVec = fwd * inp.moveDir.y + right * inp.moveDir.x;
        float wishLen = glm::length(wishVec);
        glm::vec3 wishDir = (wishLen > 0.001f) ? wishVec / wishLen : glm::vec3(0.0f);

        // ---- 2. Coyote time --------------------------------------------------
        ctrl.coyoteTimer = wasOnGround ? cfg.coyoteTime : std::max(ctrl.coyoteTimer - dt, 0.0f);
        bool canJump = ctrl.onGround || ctrl.coyoteTimer > 0.0f;

        // ---- 3. Timers -------------------------------------------------------
        if (ctrl.wallCooldown > 0.0f)
            ctrl.wallCooldown -= dt;
        if (ctrl.onGround) {
            ctrl.gliding = false;
            ctrl.glideTimer = 0.0f;
        }

        // ---- 4. Wall-run update (WallRun component, if present) --------------
        WallRun* wr = reg.try_get<WallRun>(entity);
        if (wr) {
            wr->cooldown = std::max(wr->cooldown - dt, 0.0f);

            if (wr->active) {
                wr->timer += dt;
                float hspd = glm::length(glm::vec2(vel.linear.x, vel.linear.z));
                if (ctrl.onGround || wr->timer > WallRun::k_maxDuration || hspd < WallRun::k_minSpeed * 0.5f)
                    wr->active = false;
            }

            if (!wr->active && !ctrl.onGround && wr->cooldown <= 0.0f) {
                float hspd = glm::length(glm::vec2(vel.linear.x, vel.linear.z));
                glm::vec3 detectedNormal;
                if (hspd >= WallRun::k_minSpeed &&
                    detectWallRun(
                        tf.position, {ctrl.halfWidth, ctrl.halfHeight, ctrl.halfWidth}, world, fwd, detectedNormal))
                {
                    wr->active = true;
                    wr->timer = 0.0f;
                    wr->wallNormal = detectedNormal;
                    wr->wallTangent = glm::normalize(projectOnPlane(fwd, detectedNormal) -
                                                     glm::vec3(0, projectOnPlane(fwd, detectedNormal).y, 0));
                }
            }
        }

        // ---- 5. Gravity (modified by wall run / glide) ----------------------
        float effectiveGravity = gravity;

        if (wr && wr->active) {
            effectiveGravity = gravity * 0.08f;
        } else if (!ctrl.onGround) {
            if (inp.glideHeld && !wasOnGround && ctrl.glideTimer < cfg.glideMaxTime &&
                glm::dot(vel.linear, ctrl.gravityDir) > 0.0f)
            {
                ctrl.gliding = true;
                ctrl.glideTimer = std::min(ctrl.glideTimer + dt, cfg.glideMaxTime);
                effectiveGravity *= cfg.glideGravityScale;
            } else {
                ctrl.gliding = false;
            }
        }

        if (!ctrl.onGround)
            vel.linear += ctrl.gravityDir * effectiveGravity * dt;

        // ---- 6. Movement acceleration ----------------------------------------
        if (ctrl.onGround) {
            applyFriction(vel.linear, up, cfg.stopSpeed, cfg.friction, dt);
            float wishSpeed = cfg.maxSpeedGround * wishLen;
            accelerate(vel.linear, wishDir, wishSpeed, cfg.accelGround, dt);
        } else if (wr && wr->active) {
            float wishSpeed = cfg.maxSpeedGround * wishLen;
            accelerate(vel.linear, wishDir, wishSpeed, cfg.accelGround, dt);
        } else if (ctrl.onSurf) {
            float wishSpeed = cfg.maxSpeedGround * wishLen;
            accelerate(vel.linear, wishDir, wishSpeed, cfg.accelAir * 1.5f, dt);
        } else {
            float wishSpeed = std::min(cfg.maxSpeedAir, cfg.maxSpeedGround * wishLen);
            accelerate(vel.linear, wishDir, wishSpeed, cfg.accelAir, dt);
        }

        clampSpeed(vel.linear, cfg.maxVelocity);

        // ---- 7. Jump ---------------------------------------------------------
        bool doJump = canJump && (inp.jumpPressed || (cfg.autoBhop && inp.jumpHeld));
        if (doJump) {
            float downVel = glm::dot(vel.linear, ctrl.gravityDir);
            if (downVel > 0.0f)
                vel.linear -= ctrl.gravityDir * downVel;
            vel.linear += up * cfg.jumpSpeed;
            ctrl.onGround = false;
            ctrl.coyoteTimer = 0.0f;
            if (wr)
                wr->active = false;
        }

        // ---- 8. Wall-run jump ------------------------------------------------
        bool doWallRunJump = wr && wr->active && inp.jumpPressed;
        if (doWallRunJump) {
            glm::vec3 kickDir = glm::normalize(wr->wallNormal + up * 0.7f);
            vel.linear = kickDir * cfg.wallJumpSpeed * 1.5f +
                         wr->wallTangent * glm::length(glm::vec2(vel.linear.x, vel.linear.z)) * 0.8f;
            wr->active = false;
            wr->cooldown = cfg.wallCooldown;
        }

        // ---- 9. Classic wall jump (collision-detected wall) ------------------
        bool doWalljump = !ctrl.onGround && ctrl.onWall && ctrl.wallCooldown <= 0.0f && inp.jumpPressed;
        if (!doWallRunJump && doWalljump) {
            glm::vec3 lateralVel = projectOnPlane(vel.linear, up);
            glm::vec3 reflected = glm::reflect(lateralVel, ctrl.wallNormal);
            vel.linear = reflected * cfg.wallJumpLateral + up * cfg.wallJumpSpeed;
            ctrl.wallCooldown = cfg.wallCooldown;
            ctrl.onWall = false;
        }

        // ---- 10. Grapple hook -----------------------------------------------
        GrappleHook* gh = reg.try_get<GrappleHook>(entity);
        if (gh) {
            glm::vec3 eyePos = tf.position + glm::vec3(0.0f, ctrl.eyeHeight, 0.0f);
            float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
            float sy2 = std::sin(cam.yaw), cy2 = std::cos(cam.yaw);
            glm::vec3 aimDir = glm::normalize(glm::vec3(-sy2 * cp, sp, cy2 * cp));

            updateGrappleHook(
                *gh, tf.position, vel, ctrl, eyePos, aimDir, inp.grappleHeld, inp.grapplePressed, world, dt);
        }

        // ---- 11+12. Substep position integration + collision resolution ------
        //
        // 16 substeps keep per-step displacement ≤ 7 qu so we can't tunnel
        // through thin walls (8 qu) or cause wrong-axis ejections.
        //
        // CRITICAL: resolveCollisions resets onGround/onWall at each call.
        // When standing still, substep 1 detects the floor and zeroes vel.y;
        // substeps 2-16 then see zero overlap (player at exact floor surface)
        // and leave onGround=false. We accumulate flags across all substeps
        // with OR so the frame ends with the correct contact state.
        //
        {
            const glm::vec3 half{ctrl.halfWidth, ctrl.halfHeight, ctrl.halfWidth};
            constexpr int k_substeps = 16;
            const float subDt = dt / static_cast<float>(k_substeps);

            bool accGround = false;
            bool accWall = false;
            glm::vec3 accGroundNorm = ctrl.groundNormal;
            glm::vec3 accWallNorm = ctrl.wallNormal;

            for (int sub = 0; sub < k_substeps; ++sub) {
                tf.position += vel.linear * subDt;
                tf.position = resolveCollisions(tf.position, half, world, vel, ctrl);

                if (ctrl.onGround) {
                    accGround = true;
                    accGroundNorm = ctrl.groundNormal;
                }
                if (ctrl.onWall) {
                    accWall = true;
                    accWallNorm = ctrl.wallNormal;
                }
            }

            // Write accumulated contact state back so the next frame's
            // movement calculations (friction, acceleration, coyote, jump) are correct.
            ctrl.onGround = accGround;
            ctrl.groundNormal = accGroundNorm;
            ctrl.onWall = accWall;
            ctrl.wallNormal = accWallNorm;
        }
    }
}
