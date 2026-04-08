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

void runCollision(Registry& registry, float dt, std::span<const physics::Plane> planes)
{
    registry.view<Position, Velocity, CollisionShape, PlayerState>().each(
        [dt, planes](Position& pos, Velocity& vel, const CollisionShape& shape, PlayerState& state) {
            // Reset each tick. CollisionSystem is the only writer.
            state.grounded = false;

            float remainingTime = dt;

            // Up to 4 bump iterations — handles corners and multi-surface contacts
            // the same way Quake's PM_SlideMove does.
            for (int clip = 0; clip < 4 && remainingTime > 1e-5f; ++clip) {
                const glm::vec3 k_target = pos.value + vel.value * remainingTime;
                const physics::HitResult k_hit = physics::sweepAABB(shape.halfExtents, pos.value, k_target, planes);

                if (!k_hit.hit) {
                    // Clear path — move the full remaining distance.
                    pos.value = k_target;
                    break;
                }

                // Move to the contact point, then consume that fraction of time.
                pos.value += vel.value * k_hit.tFirst * remainingTime;
                remainingTime *= (1.0f - k_hit.tFirst);

                // Push slightly off the surface (Quake's DIST_EPSILON = 1/32).
                // Without this, floating-point error can leave the entity
                // 1–2 ULP below k_r, causing the next tick's sweep to skip
                // the plane entirely and the entity to fall through.
                pos.value += k_hit.normal * 0.03125f;

                // Classify the surface and pick the correct overbounce.
                const bool k_isFloor = k_hit.normal.y > 0.7f;
                const float k_overbounce = k_isFloor ? physics::k_overbounceFloor : physics::k_overbounceWall;

                if (k_isFloor)
                    state.grounded = true;

                // Clip velocity to slide along the surface.
                vel.value = physics::clipVelocity(vel.value, k_hit.normal, k_overbounce);
            }
        });
}

} // namespace systems
