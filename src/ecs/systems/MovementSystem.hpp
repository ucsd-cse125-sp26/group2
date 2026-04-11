/// @file MovementSystem.hpp
/// @brief Shared movement system implementing the Titanfall-inspired state machine.

#pragma once

#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"

/// @brief Shared movement system — compiled identically on client and server.
///
/// Any divergence between client and server builds is a bug (breaks prediction).
///
/// Implements the full Titanfall-inspired movement state machine:
///   OnFoot → Sliding → WallRunning → Climbing → LedgeGrabbing
/// with sprint, double jump, coyote time, jump lurch, air strafing, and speed cap.
///
/// @note Position integration is NOT done here — CollisionSystem owns that via swept AABB.
namespace systems
{

/// @brief Apply one tick of player movement physics to all eligible entities.
///
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
/// @param world     World collision geometry (needed for wall/climb/ledge detection).
void runMovement(Registry& registry, float dt, const physics::WorldGeometry& world);

} // namespace systems
