/// @file HitboxHistorySystem.hpp
/// @brief Server-only system that pushes the current `HitboxInstance` for
/// every replicated entity into its `HitboxHistory` ring once per tick.
///
/// Phase 6 scaffolding for lag-compensated hitscan. Runs immediately after
/// `updateHitboxes()` (so capsules reflect this tick's animation pose) and
/// before `runWeapon` (so the upcoming raycasts can — eventually — rewind
/// against the snapshot they just wrote). Today the ring is populated but
/// no consumer reads from it; see `LagCompensation.hpp` for the rewind
/// guard.

#pragma once

#include "ecs/registry/Registry.hpp"

#include <cstdint>

namespace systems
{

/// @brief Capture this tick's hitbox capsules into each entity's
/// `HitboxHistory` ring.
///
/// For every entity that has a `HitboxInstance`, copies its current
/// `capsules` vector into the next slot of the ring and records the
/// owning server tick. Entities without a `HitboxInstance` (e.g. dead
/// players whose capsules were dropped by `updateHitboxes`) are
/// skipped — their existing samples stay in the ring, so a target that
/// died ~200 ms ago can still be hit by a delayed shot once rewind
/// lands.
///
/// @param registry   The ECS registry.
/// @param serverTick Current server tick (monotonic, 128 Hz). Stored
///                   alongside the capsules so the rewind guard can
///                   pick the closest sample to the attacker's tick.
void pushHitboxHistory(Registry& registry, uint32_t serverTick);

} // namespace systems
