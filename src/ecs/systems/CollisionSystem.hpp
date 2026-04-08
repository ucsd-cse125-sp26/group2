#pragma once

#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"

#include <span>

// Shared system — compiled identically on client and server.
// Any divergence between the two sides is a bug.
namespace systems
{

// For every entity with [Position, Velocity, CollisionShape, PlayerState]:
//   - Clears grounded flag
//   - Sweeps the AABB from current position toward pos + vel*dt
//   - On hit: moves to contact point, clips velocity, repeats up to 4 times
//             (Quake-style bumping handles corners and multi-surface contacts)
//   - Sets grounded=true if a floor surface (normal.y > 0.7) is hit
//
// Position integration lives here, not in MovementSystem.
void runCollision(Registry& registry, float dt, std::span<const physics::Plane> planes);

} // namespace systems
