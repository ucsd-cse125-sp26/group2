#include "ecs/systems/MovementSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"

#include <glm/geometric.hpp>

namespace systems
{

namespace
{
constexpr float k_standingHalfHeight = 36.0f;
constexpr float k_crouchingHalfHeight = 22.0f;

// How far the AABB centre moves when transitioning between standing and
// crouching. The feet stay at the same world position; only the centre moves.
constexpr float k_crouchCentreShift = k_standingHalfHeight - k_crouchingHalfHeight; // 14
} // namespace

void runMovement(Registry& registry, float dt)
{
    // -------------------------------------------------------------------------
    // Entities WITH InputSnapshot — full player movement.
    // -------------------------------------------------------------------------
    registry.view<Position, Velocity, PlayerState, CollisionShape, InputSnapshot>().each(
        [dt](Position& pos, Velocity& vel, PlayerState& state, CollisionShape& shape, const InputSnapshot& input) {
            // -- Crouch transition -------------------------------------------
            // Position represents the AABB centre. When halfExtents.y changes,
            // the centre must shift so the feet stay at the same world position:
            //   feet = pos.y - halfExtents.y  (must remain constant)
            //
            // Stand → Crouch: halfExtents shrinks → lower centre by delta.
            // Crouch → Stand: halfExtents grows  → raise centre by delta.
            //
            // Without this adjustment, the entity ends up geometrically inside
            // the floor after uncrouching, causing the sweep to skip the plane
            // entirely and the entity to fall through.
            const bool k_wantsCrouch = input.crouch;
            if (k_wantsCrouch && !state.crouching) {
                // Transitioning to crouch: lower centre to keep feet in place.
                state.crouching = true;
                shape.halfExtents.y = k_crouchingHalfHeight;
                pos.value.y -= k_crouchCentreShift;
            } else if (!k_wantsCrouch && state.crouching) {
                // Transitioning to stand: raise centre to keep feet in place.
                // CollisionSystem's depenetration pass will push the entity back
                // down if raising it puts the top through a ceiling.
                state.crouching = false;
                shape.halfExtents.y = k_standingHalfHeight;
                pos.value.y += k_crouchCentreShift;
            }

            // -- Jump ---------------------------------------------------------
            if (input.jump && state.grounded && !state.crouching) {
                vel.value.y = physics::k_jumpSpeed;
                state.grounded = false;
            }

            // -- Wish direction -----------------------------------------------
            const glm::vec3 k_wishDir =
                physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);

            // -- Ground movement ----------------------------------------------
            if (state.grounded) {
                vel.value = physics::applyGroundFriction(vel.value, dt);

                if (glm::length(k_wishDir) > 0.001f)
                    vel.value = physics::accelerate(
                        vel.value, k_wishDir, physics::k_maxGroundSpeed, physics::k_groundAccel, dt);
            }
            // -- Air movement -------------------------------------------------
            else {
                vel.value = physics::applyGravity(vel.value, dt);

                if (glm::length(k_wishDir) > 0.001f)
                    vel.value =
                        physics::accelerate(vel.value, k_wishDir, physics::k_airMaxSpeed, physics::k_airAccel, dt);
            }
        });

    // -------------------------------------------------------------------------
    // Entities WITHOUT InputSnapshot — physics only (gravity / friction).
    // -------------------------------------------------------------------------
    registry.view<Velocity, PlayerState>(entt::exclude<InputSnapshot>)
        .each([dt](Velocity& vel, const PlayerState& state) {
            if (state.grounded)
                vel.value = physics::applyGroundFriction(vel.value, dt);
            else
                vel.value = physics::applyGravity(vel.value, dt);
        });
}

} // namespace systems
