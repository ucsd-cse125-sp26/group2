#include "Physics.hpp"

#include "../ecs/Components.hpp"

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
// Adds velocity along wishDir, but only up to wishSpeed.
// Because we project current vel onto wishDir first, strafing sideways adds
// speed without clipping the total (key to building bhop speed).
// ---------------------------------------------------------------------------
void accelerate(glm::vec3& vel, glm::vec3 wishDir, float wishSpeed, float accel, float dt)
{
    float currentSpeed = glm::dot(vel, wishDir);
    float addSpeed     = wishSpeed - currentSpeed;
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
    float speed          = glm::length(lateralVel);
    if (speed < 0.001f) {
        vel -= lateralVel; // kill lateral movement completely
        return;
    }
    float control  = std::max(speed, stopSpeed);
    float drop     = control * friction * dt;
    float newSpeed = std::max(speed - drop, 0.0f);
    vel -= lateralVel * (1.0f - newSpeed / speed);
}

// ---------------------------------------------------------------------------
// AABB overlap test.
// Returns true and writes penetration depth + axis if overlapping.
// ---------------------------------------------------------------------------
bool aabbOverlap(glm::vec3 aCenter, glm::vec3 aHalf, glm::vec3 bCenter, glm::vec3 bHalf, glm::vec3& outDepth)
{
    glm::vec3 d       = aCenter - bCenter;
    glm::vec3 sum     = aHalf + bHalf;
    glm::vec3 overlap = sum - glm::abs(d);

    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f)
        return false;

    outDepth = overlap;
    return true;
}

// ---------------------------------------------------------------------------
// Resolve AABB vs world along a single axis, writing collision info back
// into PlayerController. Returns the displacement needed to push out.
// ---------------------------------------------------------------------------
glm::vec3 resolveCollisions(glm::vec3 pos, glm::vec3 half, const World& world, Velocity& vel, PlayerController& ctrl)
{
    constexpr float k_groundDot = 0.70f; // cos(45°) — surfaces above this are "floor"

    ctrl.onWall   = false;
    ctrl.onGround = false;

    // Sweep axis-by-axis (classic Q3 approach)
    glm::vec3 result = pos;

    // Helper: try to push out of any overlapping box
    auto sweep = [&](int axis) {
        for (const auto& box : world.boxes) {
            glm::vec3 depth;
            if (!aabbOverlap(result, half, box.center, box.half, depth))
                continue;

            // Find the minimum penetration axis that matches our current sweep axis
            // We only care about this axis right now
            float pen = (result[axis] > box.center[axis]) ? depth[axis] : -depth[axis];

            result[axis] += pen;

            // Determine surface normal (approximate)
            glm::vec3 normal{0.0f};
            normal[axis] = (pen > 0.0f) ? 1.0f : -1.0f;

            float upDot = glm::dot(normal, glm::vec3(0, 1, 0));

            if (upDot >= k_groundDot) {
                // Floor
                ctrl.onGround     = true;
                ctrl.groundNormal = normal;
                if (vel.linear[axis] < 0.0f)
                    vel.linear[axis] = 0.0f;
            } else if (upDot <= -k_groundDot) {
                // Ceiling — kill upward velocity
                if (vel.linear[axis] > 0.0f)
                    vel.linear[axis] = 0.0f;
            } else {
                // Wall
                if (!ctrl.onWall) {
                    ctrl.onWall     = true;
                    ctrl.wallNormal = normal;
                }
                // Cancel velocity into wall
                float vDotN = glm::dot(vel.linear, normal);
                if (vDotN < 0.0f)
                    vel.linear -= normal * vDotN;
            }
        }
    };

    sweep(1); // Y first (gravity axis) — sets onGround early
    sweep(0); // X
    sweep(2); // Z

    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// physicsUpdate — called every game tick from main.cpp
// ---------------------------------------------------------------------------

void physicsUpdate(entt::registry& reg, const World& world, const PhysicsConfig& cfg, float dt)
{

    auto view = reg.view<Transform, Velocity, PlayerController, InputState, CameraAngles>();

    for (auto entity : view) {
        auto& tf   = view.get<Transform>(entity);
        auto& vel  = view.get<Velocity>(entity);
        auto& ctrl = view.get<PlayerController>(entity);
        auto& inp  = view.get<InputState>(entity);
        auto& cam  = view.get<CameraAngles>(entity);

        const glm::vec3 up  = -ctrl.gravityDir; // "up" is against gravity
        const float gravity = ctrl.gravityMag;

        bool wasOnGround = ctrl.onGround;

        // ---- 1. Build wish direction from input + camera yaw ----------------
        // Input is in camera-local XZ; rotate by yaw to get world-space
        float cy = std::cos(cam.yaw), sy = std::sin(cam.yaw);
        // Forward and right vectors in the horizontal plane.
        // fwd matches the camera lookAt forward: {-sin, 0, cos}.
        // right = cross(fwd, up) = {-cos, 0, -sin}  (camera's actual right axis).
        glm::vec3 fwd   = {-sy,  0.0f,  cy};
        glm::vec3 right = {-cy,  0.0f, -sy};

        glm::vec3 wishVec = fwd * inp.moveDir.y + right * inp.moveDir.x;
        float wishLen     = glm::length(wishVec);
        glm::vec3 wishDir = (wishLen > 0.001f) ? wishVec / wishLen : glm::vec3(0.0f);

        // ---- 2. Coyote time --------------------------------------------------
        if (wasOnGround) {
            ctrl.coyoteTimer = cfg.coyoteTime;
        } else {
            ctrl.coyoteTimer = std::max(ctrl.coyoteTimer - dt, 0.0f);
        }

        bool canJump = ctrl.onGround || ctrl.coyoteTimer > 0.0f;

        // ---- 3. Walljump cooldown -------------------------------------------
        if (ctrl.wallCooldown > 0.0f)
            ctrl.wallCooldown -= dt;

        // ---- 4. Glide timer --------------------------------------------------
        // Track glide state
        if (ctrl.onGround) {
            ctrl.gliding    = false;
            ctrl.glideTimer = 0.0f;
        }

        // ---- 5. Gravity ------------------------------------------------------
        float effectiveGravity = gravity;
        if (!ctrl.onGround) {
            if (inp.glideHeld && !wasOnGround && ctrl.glideTimer < cfg.glideMaxTime &&
                glm::dot(vel.linear, ctrl.gravityDir) > 0.0f)
            {
                // Falling + glide key held → glide
                ctrl.gliding    = true;
                ctrl.glideTimer = std::min(ctrl.glideTimer + dt, cfg.glideMaxTime);
                effectiveGravity *= cfg.glideGravityScale;
            } else {
                ctrl.gliding = false;
            }
            vel.linear += ctrl.gravityDir * effectiveGravity * dt;
        }

        // ---- 6. Movement acceleration ----------------------------------------
        if (ctrl.onGround) {
            // a. Always apply friction (Source behaviour — friction runs even on the
            //    jump frame, which gives the tight snappy feel rather than icy sliding).
            applyFriction(vel.linear, up, cfg.stopSpeed, cfg.friction, dt);
            // b. Ground accelerate (full wishspeed)
            float wishSpeed = cfg.maxSpeedGround * wishLen;
            accelerate(vel.linear, wishDir, wishSpeed, cfg.accelGround, dt);
        } else {
            // Air accelerate — wishspeed capped hard (allows strafing, not raw speed)
            float wishSpeed = std::min(cfg.maxSpeedAir, cfg.maxSpeedGround * wishLen);
            accelerate(vel.linear, wishDir, wishSpeed, cfg.accelAir, dt);
        }

        clampSpeed(vel.linear, cfg.maxVelocity);

        // ---- 7. Jump ---------------------------------------------------------
        bool doJump = canJump && (inp.jumpPressed || (cfg.autoBhop && inp.jumpHeld));
        if (doJump) {
            // Bhop key: remove downward velocity component, add full jumpSpeed upward.
            // Horizontal momentum is completely preserved — this is the bhop mechanic.
            float downVel = glm::dot(vel.linear, ctrl.gravityDir);
            if (downVel > 0.0f)
                vel.linear -= ctrl.gravityDir * downVel;
            vel.linear += up * cfg.jumpSpeed;
            ctrl.onGround    = false;
            ctrl.coyoteTimer = 0.0f;
        }

        // ---- 8. Wall jump ----------------------------------------------------
        bool doWalljump = !ctrl.onGround && ctrl.onWall && ctrl.wallCooldown <= 0.0f && inp.jumpPressed;
        if (doWalljump) {
            // Reflect horizontal velocity off wall normal, boost away
            glm::vec3 lateralVel = projectOnPlane(vel.linear, up);
            glm::vec3 reflected  = glm::reflect(lateralVel, ctrl.wallNormal);
            vel.linear           = reflected * cfg.wallJumpLateral + up * cfg.wallJumpSpeed;
            ctrl.wallCooldown    = cfg.wallCooldown;
            ctrl.onWall          = false;
        }

        // ---- 9. Integrate position -------------------------------------------
        tf.position += vel.linear * dt;

        // ---- 10. Collision resolve -------------------------------------------
        glm::vec3 half{ctrl.halfWidth, ctrl.halfHeight, ctrl.halfWidth};
        tf.position = resolveCollisions(tf.position, half, world, vel, ctrl);

        // ---- 11. Auto-bhop landing (after resolve so onGround is fresh) ------
        if (ctrl.onGround && !wasOnGround) {
            // We just landed. If jump is held and autoBhop, the next frame's
            // doJump check handles it (friction is skipped when jumping).
        }
    }
}
