/// @file DynamicsSystem.hpp
/// @brief Per-tick orchestration of the rigid-body dynamics pipeline.
///
/// Sits alongside the existing kinematic `runCollision` for the player:
/// while `runCollision` does swept-AABB resolution against the static
/// world for entities with `Position+Velocity+CollisionShape+PlayerVisState`,
/// `runDynamics` runs the full PGS solver pipeline for entities that have
/// a `RigidBody` component (ragdoll bones, dropped weapons, dynamic
/// debris).
///
/// **Tick sequence** (called from `ServerGame::iterate` and client
/// prediction):
///   1. Integrate position from velocity for every awake dynamic body.
///   2. Generate contact manifolds vs. static world geometry (per-body
///      swept AABB against the active world).
///   3. Solve contacts (PGS) and joints (point + hinge + cone-twist).
///   4. Update sleep state from converged velocities.
///   5. End-of-frame: `ContactCache::endFrame()` reaps stale manifolds.
///
/// All systems are deterministic — pairs and joints iterated in sorted-by-
/// entity-id order; thread-local accumulators in the events/debug paths.

#pragma once

#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"

namespace physics
{
class ContactCache;
struct SolverConfig;
struct SleepConfig;
} // namespace physics

namespace systems
{

/// @brief One physics tick for rigid-body dynamic entities.
///
/// @param registry  ECS registry.
/// @param dt        Tick duration (typically 1/128 s).
/// @param world     Active world collision geometry (static).
/// @param cache     Persistent contact-manifold cache (carried across ticks
///                  for warm-starting the solver).
/// @param solverCfg Solver tuning parameters (iteration counts, friction).
/// @param sleepCfg  Sleep thresholds.
void runDynamics(Registry& registry,
                 float dt,
                 const physics::WorldGeometry& world,
                 physics::ContactCache& cache,
                 const physics::SolverConfig& solverCfg,
                 const physics::SleepConfig& sleepCfg);

} // namespace systems
