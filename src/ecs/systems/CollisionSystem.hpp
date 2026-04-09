#pragma once

#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"

#include <span>

/// @brief Shared collision system — compiled identically on client and server.
///
/// Any divergence between client and server builds is a bug (breaks prediction).
namespace systems
{

/// @brief Run one tick of swept-AABB collision for all physics entities.
///
/// For every entity with `[Position, Velocity, CollisionShape, PlayerState]`:
/// 1. Clears the `grounded` flag.
/// 2. Sweeps the AABB from current position toward `pos + vel * dt`.
/// 3. On hit: moves to the contact point, clips velocity, repeats up to 4 times
///    (Quake-style bumping handles corners and multi-surface contacts).
/// 4. Sets `grounded = true` if a floor surface (`normal.y > 0.7`) is hit.
///
/// @note Position integration lives here, **not** in MovementSystem.
///
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
/// @param planes    World collision planes for this tick.
void runCollision(Registry& registry, float dt, std::span<const physics::Plane> planes);

} // namespace systems
