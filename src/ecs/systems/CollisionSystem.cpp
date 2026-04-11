/// @file CollisionSystem.cpp
/// @brief Implementation of swept-AABB collision detection and response.

#include "ecs/systems/CollisionSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/SweptCollision.hpp"

#include <glm/geometric.hpp>

namespace systems
{

static constexpr float k_pushback = 0.03125f;                         // Quake DIST_EPSILON
static constexpr float k_groundProbeDistance = physics::k_stepHeight; // also used for slope snap

// Depenetration

/// @brief Push the entity out of any infinite planes it currently overlaps.
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param planes       Infinite planes to test against.
static void
depenetratePlanes(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, std::span<const physics::Plane> planes)
{
    for (const physics::Plane& plane : planes) {
        const float k_r = std::abs(plane.normal.x) * halfExtents.x + std::abs(plane.normal.y) * halfExtents.y +
                          std::abs(plane.normal.z) * halfExtents.z;
        const float k_dist = glm::dot(plane.normal, pos) - plane.distance;

        if (k_dist < k_r) {
            const float k_overlap = k_r - k_dist;
            pos += plane.normal * (k_overlap + k_pushback);

            const float k_into = glm::dot(vel, plane.normal);
            if (k_into < 0.0f)
                vel -= plane.normal * k_into;
        }
    }
}

/// @brief Push the entity out of a static AABB it currently overlaps.
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param box          Static axis-aligned bounding box to test against.
static void depenetrateBox(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldAABB& box)
{
    const glm::vec3 k_expMin = box.min - halfExtents;
    const glm::vec3 k_expMax = box.max + halfExtents;

    // Not overlapping?
    if (pos.x < k_expMin.x || pos.x > k_expMax.x || pos.y < k_expMin.y || pos.y > k_expMax.y || pos.z < k_expMin.z ||
        pos.z > k_expMax.z)
        return;

    // Find the axis of least penetration and push out.
    float minPen = 1e30f;
    glm::vec3 pushDir{0.0f};

    // clang-format off
    struct { float pen; glm::vec3 dir; } faces[] = {
        {pos.x - k_expMin.x, {-1, 0, 0}},
        {k_expMax.x - pos.x, { 1, 0, 0}},
        {pos.y - k_expMin.y, { 0,-1, 0}},
        {k_expMax.y - pos.y, { 0, 1, 0}},
        {pos.z - k_expMin.z, { 0, 0,-1}},
        {k_expMax.z - pos.z, { 0, 0, 1}},
    };
    // clang-format on

    for (const auto& f : faces) {
        if (f.pen < minPen) {
            minPen = f.pen;
            pushDir = f.dir;
        }
    }

    pos += pushDir * (minPen + k_pushback);
    const float k_into = glm::dot(vel, pushDir);
    if (k_into < 0.0f)
        vel -= pushDir * k_into;
}

/// @brief Push the entity out of a convex brush it currently overlaps.
/// Only fires if the entity is inside ALL planes simultaneously.
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param brush        Convex brush to test against.
static void
depenetrateBrush(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldBrush& brush)
{
    float minOverlap = 1e30f;
    int minPlane = -1;

    for (int i = 0; i < brush.planeCount; ++i) {
        const auto& p = brush.planes[i];
        const float k_r = std::abs(p.normal.x) * halfExtents.x + std::abs(p.normal.y) * halfExtents.y +
                          std::abs(p.normal.z) * halfExtents.z;
        const float k_dist = glm::dot(p.normal, pos) - p.distance;

        if (k_dist >= k_r)
            return; // outside this plane → not inside brush

        const float k_overlap = k_r - k_dist;
        if (k_overlap < minOverlap) {
            minOverlap = k_overlap;
            minPlane = i;
        }
    }

    if (minPlane < 0)
        return;

    const auto& plane = brush.planes[minPlane];
    pos += plane.normal * (minOverlap + k_pushback);

    const float k_into = glm::dot(vel, plane.normal);
    if (k_into < 0.0f)
        vel -= plane.normal * k_into;
}

/// @brief Run all depenetration passes (planes, boxes, brushes).
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param world        World collision geometry.
static void
depenetrate(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldGeometry& world)
{
    depenetratePlanes(pos, vel, halfExtents, world.planes);

    for (const physics::WorldAABB& box : world.boxes)
        depenetrateBox(pos, vel, halfExtents, box);

    for (const physics::WorldBrush& brush : world.brushes)
        depenetrateBrush(pos, vel, halfExtents, brush);
}

/// @brief Attempt to step over a low obstacle when a wall is hit.
/// @param pos            Entity position (modified in place on success).
/// @param vel            Entity velocity (modified in place on success).
/// @param halfExtents    AABB half-extents of the entity.
/// @param remainingTime  Time remaining in the current bump iteration.
/// @param world          World collision geometry.
/// @return True if the step succeeded and position/velocity were updated.
static bool tryStepUp(glm::vec3& pos,
                      glm::vec3& vel,
                      const glm::vec3& halfExtents,
                      float remainingTime,
                      const physics::WorldGeometry& world)
{
    const glm::vec3 k_stepVec{0.0f, physics::k_stepHeight, 0.0f};

    // 1. Lift straight up — abort if ceiling blocks.
    const glm::vec3 k_liftEnd = pos + k_stepVec;
    const physics::HitResult k_lift = physics::sweepAll(halfExtents, pos, k_liftEnd, world);
    if (k_lift.hit)
        return false;

    // 2. Sweep horizontally at step height — abort if still blocked.
    const glm::vec3 k_horizEnd = k_liftEnd + glm::vec3{vel.x * remainingTime, 0.0f, vel.z * remainingTime};
    const physics::HitResult k_horiz = physics::sweepAll(halfExtents, k_liftEnd, k_horizEnd, world);
    if (k_horiz.hit)
        return false;

    // 3. Drop back down — must land on a floor-like surface.
    const glm::vec3 k_dropEnd = k_horizEnd - k_stepVec;
    const physics::HitResult k_drop = physics::sweepAll(halfExtents, k_horizEnd, k_dropEnd, world);

    if (!k_drop.hit || k_drop.normal.y <= 0.7f)
        return false;

    pos = k_horizEnd - k_stepVec * k_drop.tFirst;
    pos += k_drop.normal * k_pushback;
    vel.y = 0.0f;
    return true;
}

/// @brief Keep the entity glued to descending slopes and step-downs.
/// @param pos          Entity position (modified in place).
/// @param vel          Entity velocity (modified in place).
/// @param halfExtents  AABB half-extents of the entity.
/// @param world        World collision geometry.
static void
snapToGround(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, const physics::WorldGeometry& world)
{
    const glm::vec3 k_probeTarget = pos - glm::vec3{0.0f, k_groundProbeDistance, 0.0f};
    const physics::HitResult k_snap = physics::sweepAll(halfExtents, pos, k_probeTarget, world);

    if (!k_snap.hit || k_snap.normal.y <= 0.7f)
        return;

    pos = pos - glm::vec3{0.0f, k_groundProbeDistance * k_snap.tFirst, 0.0f};
    pos += k_snap.normal * k_pushback;
    vel.y = 0.0f;
}

void runCollision(Registry& registry, float dt, const physics::WorldGeometry& world)
{
    registry.view<Position, Velocity, CollisionShape, PlayerState>().each(
        [dt, &world](Position& pos, Velocity& vel, const CollisionShape& shape, PlayerState& state) {
            const bool k_wasGrounded = state.grounded;
            state.grounded = false;

            // Phase 0 — Depenetration
            depenetrate(pos.value, vel.value, shape.halfExtents, world);

            // Phase 1 — Bump loop (collision response + stair stepping)
            float remainingTime = dt;

            for (int clip = 0; clip < 4 && remainingTime > 1e-5f; ++clip) {
                const glm::vec3 k_target = pos.value + vel.value * remainingTime;
                const physics::HitResult k_hit = physics::sweepAll(shape.halfExtents, pos.value, k_target, world);

                if (!k_hit.hit) {
                    pos.value = k_target;
                    break;
                }

                pos.value += vel.value * k_hit.tFirst * remainingTime;
                remainingTime *= (1.0f - k_hit.tFirst);

                const bool k_isFloor = k_hit.normal.y > 0.7f;

                if (k_isFloor) {
                    pos.value += k_hit.normal * k_pushback;
                    vel.value = physics::clipVelocity(vel.value, k_hit.normal, physics::k_overbounceFloor);
                    state.grounded = true;
                    state.groundNormal = k_hit.normal;
                } else {
                    if (k_wasGrounded && tryStepUp(pos.value, vel.value, shape.halfExtents, remainingTime, world)) {
                        state.grounded = true;
                        break;
                    }
                    pos.value += k_hit.normal * k_pushback;
                    vel.value = physics::clipVelocity(vel.value, k_hit.normal, physics::k_overbounceWall);
                }
            }

            // Phase 2 — Slope sticking
            if (k_wasGrounded) {
                const float k_horizSpeed = glm::length(glm::vec3{vel.value.x, 0.0f, vel.value.z});
                if (k_horizSpeed > 0.001f)
                    snapToGround(pos.value, vel.value, shape.halfExtents, world);
            }

            // Phase 3 — Ground probe
            const glm::vec3 k_probeTarget = pos.value - glm::vec3{0.0f, k_groundProbeDistance, 0.0f};
            const physics::HitResult k_probe = physics::sweepAll(shape.halfExtents, pos.value, k_probeTarget, world);

            if (k_probe.hit && k_probe.normal.y > 0.7f) {
                state.grounded = true;
                state.groundNormal = k_probe.normal;
            }
        });
}

} // namespace systems
