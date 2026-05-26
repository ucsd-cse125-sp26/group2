/// @file KillzoneSystem.hpp
/// @brief Killzone trigger system: kill any player overlapping a killzone.

#pragma once

#include "ecs/registry/Registry.hpp"
#include "network/NetKillEvent.hpp"

#include <vector>

namespace systems
{

/// @brief Tick killzones: for each killzone trigger, any player whose
/// AABB overlaps it takes lethal damage. Kill events are appended to
/// `killEvents` so they're broadcast alongside other kills in the tick.
///
/// Killer is set to the player themselves so the kill feed shows the
/// kill as a self-elimination (the world has no entity to credit).
///
/// @param registry   The ECS registry.
/// @param killEvents Network kill events to broadcast this tick.
void runKillzones(Registry& registry, std::vector<NetKillEvent>& killEvents);

} // namespace systems
