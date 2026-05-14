/// @file DeterminismHash.hpp
/// @brief Deterministic state-hash for CI golden testing.
///
/// Walks every replicated physics component on every entity in a stable
/// (entity-id-sorted) order and folds their bits into a 64-bit FNV-1a
/// hash.  Two runs producing the same hash → bit-equal physics state.
///
/// Intended use:
///   1. CI runs a fixed-input scenario for N ticks twice; hashes must match.
///   2. Client + server hash the same tick's state; they must match
///      (otherwise prediction is diverging).
///
/// **Cost.** O(entity count) per call, ~50 ns per entity on a typical
/// CPU.  Cheap enough to run every frame in dev / debug builds.

#pragma once

#include "ecs/registry/Registry.hpp"

#include <cstdint>

namespace physics::diag
{

/// @brief Compute a deterministic hash of physics-relevant ECS state.
/// Includes: Position, Velocity, Orientation, AngularVelocity, RigidBody
/// accumulators (treating sleeping bodies the same as awake ones).
///
/// Order-independent across thread scheduling: entities are sorted by
/// stable id before bytes are folded into the hash.
[[nodiscard]] uint64_t hashPhysicsState(const Registry& registry) noexcept;

} // namespace physics::diag
