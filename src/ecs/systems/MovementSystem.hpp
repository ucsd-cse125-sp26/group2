#pragma once

#include "ecs/registry/Registry.hpp"

// Shared system — compiled identically on client and server.
// Any divergence between the two sides is a bug.
namespace systems
{

// For every entity with [Position, Velocity, PlayerState]:
//   - airborne → applyGravity
//   - grounded → applyGroundFriction
//   - always   → pos += vel * dt
void runMovement(Registry& registry, float dt);

} // namespace systems
