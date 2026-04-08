#pragma once

#include "ecs/registry/Registry.hpp"

// Shared system — compiled identically on client and server.
// Any divergence between the two sides is a bug.
//
// Reads:  InputSnapshot (optional), PlayerState, CollisionShape
// Writes: Velocity, PlayerState (crouching/sliding), CollisionShape (crouch resize)
//
// Position integration is NOT done here — CollisionSystem owns that via swept AABB.
namespace systems
{

void runMovement(Registry& registry, float dt);

} // namespace systems
