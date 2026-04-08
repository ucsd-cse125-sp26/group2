#include "ecs/systems/CollisionSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/SweptCollision.hpp"

namespace systems
{

// Distance the bump loop pushes entities off a surface after each contact.
// Prevents floating-point error from leaving the entity 1–2 ULP inside the
// plane on the next tick, which would cause the sweep to skip it.
static constexpr float k_pushback = 0.03125f; // Quake's DIST_EPSILON = 1/32

// How far below the current position to probe for a floor each tick.
// Must be > k_pushback so the probe always reaches past the pushback gap.
// Intentionally larger than one tick of gravity-induced velocity so we
// detect ground even when the entity is resting with near-zero velocity.
static constexpr float k_groundProbeDistance = 0.1f;

void runCollision(Registry& registry, float dt, std::span<const physics::Plane> planes)
{
    registry.view<Position, Velocity, CollisionShape, PlayerState>().each(
        [dt, planes](Position& pos, Velocity& vel, const CollisionShape& shape, PlayerState& state) {
            // ----------------------------------------------------------------
            // Phase 1 — Bump loop (collision response)
            //
            // Sweeps the AABB along the intended movement path and resolves
            // any contacts by moving to the contact point, clipping velocity,
            // and retrying with the remaining time. Up to 4 iterations handles
            // corners and triple-surface contacts (Quake PM_SlideMove style).
            //
            // This phase answers: "where does the entity end up this tick?"
            // It does NOT set grounded — see Phase 2.
            // ----------------------------------------------------------------
            state.grounded = false;

            float remainingTime = dt;

            for (int clip = 0; clip < 4 && remainingTime > 1e-5f; ++clip) {
                const glm::vec3 k_target = pos.value + vel.value * remainingTime;
                const physics::HitResult k_hit = physics::sweepAABB(shape.halfExtents, pos.value, k_target, planes);

                if (!k_hit.hit) {
                    pos.value = k_target;
                    break;
                }

                // Move to the contact point.
                pos.value += vel.value * k_hit.tFirst * remainingTime;
                remainingTime *= (1.0f - k_hit.tFirst);

                // Push slightly off the surface so floating-point error cannot
                // leave the entity inside the plane on the next tick.
                pos.value += k_hit.normal * k_pushback;

                // Clip velocity to slide along the surface.
                const bool k_isFloor = k_hit.normal.y > 0.7f;
                const float k_overbounce = k_isFloor ? physics::k_overbounceFloor : physics::k_overbounceWall;

                vel.value = physics::clipVelocity(vel.value, k_hit.normal, k_overbounce);
            }

            // ----------------------------------------------------------------
            // Phase 2 — Ground probe (grounded detection)
            //
            // This phase answers a different question from Phase 1:
            // "is the entity currently standing on a floor surface?"
            //
            // The bump loop cannot reliably answer this because when the entity
            // is at rest (vel.y ≈ 0) the sweep produces no hit — it isn't
            // moving toward the plane — so grounded would flip false every
            // other tick, causing gravity to kick in and producing a visible
            // bounce / oscillation.
            //
            // The fix: after movement is resolved, cast the AABB straight down
            // by k_groundProbeDistance. If a floor surface is within that
            // distance, the entity is grounded. The probe never moves the
            // entity or clips velocity — it only sets the flag.
            // ----------------------------------------------------------------
            const glm::vec3 k_probeTarget = pos.value - glm::vec3{0.0f, k_groundProbeDistance, 0.0f};

            const physics::HitResult k_probe = physics::sweepAABB(shape.halfExtents, pos.value, k_probeTarget, planes);

            if (k_probe.hit && k_probe.normal.y > 0.7f)
                state.grounded = true;
        });
}

} // namespace systems
