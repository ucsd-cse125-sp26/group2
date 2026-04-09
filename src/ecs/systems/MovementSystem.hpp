#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Shared movement system — compiled identically on client and server.
///
/// Any divergence between client and server builds is a bug (breaks prediction).
///
/// **Reads:** InputSnapshot (optional), PlayerState, CollisionShape
/// **Writes:** Velocity, PlayerState (crouching/sliding), CollisionShape (crouch resize)
///
/// @note Position integration is NOT done here — CollisionSystem owns that via swept AABB.
namespace systems
{

/// @brief Apply one tick of player movement physics to all eligible entities.
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runMovement(Registry& registry, float dt);

} // namespace systems
