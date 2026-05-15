/// @file TriggerSystem.hpp
/// @brief Per-tick trigger-volume overlap detection.
///
/// Run after `runCollision` (which has moved entities to their final
/// positions for the tick).  For every entity with a `TriggerVolume` +
/// `Position` + `CollisionShape`, computes the set of non-trigger entities
/// currently overlapping its AABB, diffs against last tick's set, and
/// emits `Enter` / `Stay` / `Exit` events to `physics::events`.
///
/// Determinism: triggers are iterated in `entt` view order (ascending
/// entity id by default with stable archetype views), and within each
/// trigger the overlap set is kept sorted by id.  No event ordering depends
/// on threading.

#pragma once

#include "ecs/registry/Registry.hpp"

namespace systems
{

/// @brief Test every trigger-volume entity for overlap with non-trigger
/// entities and emit `Enter` / `Stay` / `Exit` events.  Drops trigger
/// volumes' overlap state when the trigger entity is destroyed.
///
/// @param registry          ECS registry.
/// @param isPredictedClient True when called from the client-side prediction
///                          path.  Triggers without `fireOnPredictedClient`
///                          do not emit events in this mode (they would
///                          double-fire when the server's authoritative
///                          state lands).
void runTriggers(Registry& registry, bool isPredictedClient = false);

} // namespace systems
