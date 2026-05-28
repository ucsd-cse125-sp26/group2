/// @file JumpPadSystem.hpp
/// @brief Jump pad trigger system: launch overlapping players upward.

#pragma once

#include "ecs/registry/Registry.hpp"

namespace systems
{

/// @brief Tick jump pads: for each pad, any player whose AABB overlaps
/// the pad's trigger volume on the rising edge has the pad's `velocity`
/// applied as an impulse. A small per-player cooldown prevents the same
/// player from being relaunched every tick while they're still inside
/// the trigger (the impulse won't have moved them out yet).
///
/// @param registry The ECS registry.
/// @param dt       Fixed physics delta time in seconds.
void runJumpPads(Registry& registry, float dt);

} // namespace systems
