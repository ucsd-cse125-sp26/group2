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

/// @brief Push the entity out of any planes it currently overlaps.
///
/// Runs before the bump loop. If the entity starts inside any plane (from shape
/// changes like uncrouch, spawn, or floating-point accumulation), push it out
/// along the plane normal until it is clearly outside.
///
/// Without this, sweepAABB's `if (distStart < k_r) continue` guard silently
/// skips penetrated planes, allowing the entity to fall through geometry.
static void
depenetrate(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, std::span<const physics::Plane> planes)
{
    for (const physics::Plane& plane : planes) {
        const float k_r = std::abs(plane.normal.x) * halfExtents.x + std::abs(plane.normal.y) * halfExtents.y +
                          std::abs(plane.normal.z) * halfExtents.z;
        const float k_dist = glm::dot(plane.normal, pos) - plane.distance;

        if (k_dist < k_r) {
            // Push entity out so it sits just outside the surface.
            const float k_overlap = k_r - k_dist;
            pos += plane.normal * (k_overlap + k_pushback);

            // Kill velocity into the plane so the entity doesn't immediately
            // re-penetrate on the next tick.
            const float k_into = glm::dot(vel, plane.normal);
            if (k_into < 0.0f)
                vel -= plane.normal * k_into;
        }
    }
}

/// @brief Attempt to step over a low obstacle when a wall is hit.
/// @return True if the step succeeded and position/velocity were updated.
static bool tryStepUp(glm::vec3& pos,
                      glm::vec3& vel,
                      const glm::vec3& halfExtents,
                      float remainingTime,
                      std::span<const physics::Plane> planes)
{
    const glm::vec3 k_stepVec{0.0f, physics::k_stepHeight, 0.0f};

    // 1. Lift straight up — abort if ceiling blocks.
    const glm::vec3 k_liftEnd = pos + k_stepVec;
    const physics::HitResult k_lift = physics::sweepAABB(halfExtents, pos, k_liftEnd, planes);
    if (k_lift.hit)
        return false;

    // 2. Sweep horizontally at step height — abort if still blocked.
    const glm::vec3 k_horizEnd = k_liftEnd + glm::vec3{vel.x * remainingTime, 0.0f, vel.z * remainingTime};
    const physics::HitResult k_horiz = physics::sweepAABB(halfExtents, k_liftEnd, k_horizEnd, planes);
    if (k_horiz.hit)
        return false;

    // 3. Drop back down — must land on a floor-like surface.
    const glm::vec3 k_dropEnd = k_horizEnd - k_stepVec;
    const physics::HitResult k_drop = physics::sweepAABB(halfExtents, k_horizEnd, k_dropEnd, planes);

    if (!k_drop.hit || k_drop.normal.y <= 0.7f)
        return false;

    pos = k_horizEnd - k_stepVec * k_drop.tFirst;
    pos += k_drop.normal * k_pushback;
    vel.y = 0.0f;
    return true;
}

/// @brief Keep the entity glued to descending slopes and step-downs.
static void
snapToGround(glm::vec3& pos, glm::vec3& vel, const glm::vec3& halfExtents, std::span<const physics::Plane> planes)
{
    const glm::vec3 k_probeTarget = pos - glm::vec3{0.0f, k_groundProbeDistance, 0.0f};
    const physics::HitResult k_snap = physics::sweepAABB(halfExtents, pos, k_probeTarget, planes);

    if (!k_snap.hit || k_snap.normal.y <= 0.7f)
        return;

    pos = pos - glm::vec3{0.0f, k_groundProbeDistance * k_snap.tFirst, 0.0f};
    pos += k_snap.normal * k_pushback;
    vel.y = 0.0f;
}

void runCollision(Registry& registry, float dt, std::span<const physics::Plane> planes)
{
    registry.view<Position, Velocity, CollisionShape, PlayerState>().each(
        [dt, planes](Position& pos, Velocity& vel, const CollisionShape& shape, PlayerState& state) {
            const bool k_wasGrounded = state.grounded;
            state.grounded = false;

            // Phase 0 — Depenetration
            // Fix overlap introduced by shape changes (crouch/uncrouch), spawn, or
            // floating-point accumulation. Must run before the bump loop.
            depenetrate(pos.value, vel.value, shape.halfExtents, planes);

            // Phase 1 — Bump loop (collision response + stair stepping)
            float remainingTime = dt;

            for (int clip = 0; clip < 4 && remainingTime > 1e-5f; ++clip) {
                const glm::vec3 k_target = pos.value + vel.value * remainingTime;
                const physics::HitResult k_hit = physics::sweepAABB(shape.halfExtents, pos.value, k_target, planes);

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
                } else {
                    if (k_wasGrounded && tryStepUp(pos.value, vel.value, shape.halfExtents, remainingTime, planes)) {
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
                    snapToGround(pos.value, vel.value, shape.halfExtents, planes);
            }

            // Phase 3 — Ground probe
            const glm::vec3 k_probeTarget = pos.value - glm::vec3{0.0f, k_groundProbeDistance, 0.0f};
            const physics::HitResult k_probe = physics::sweepAABB(shape.halfExtents, pos.value, k_probeTarget, planes);

            if (k_probe.hit && k_probe.normal.y > 0.7f)
                state.grounded = true;
        });
}

} // namespace systems
