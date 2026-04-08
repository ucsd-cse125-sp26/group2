#include "ecs/systems/MovementSystem.hpp"

#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Movement.hpp"

namespace systems
{

void runMovement(Registry& registry, float dt)
{
    // Velocity only — position integration is done by CollisionSystem via swept AABB.
    registry.view<Position, Velocity, PlayerState>().each([dt](Position& /*pos*/, Velocity& vel, PlayerState& state) {
        if (state.grounded)
            vel.value = physics::applyGroundFriction(vel.value, dt);
        else
            vel.value = physics::applyGravity(vel.value, dt);
    });
}

} // namespace systems
