/// @file DynamicsSystem.cpp
/// @brief Implementation of the rigid-body dynamics tick.

#include "ecs/systems/DynamicsSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/ContactCache.hpp"
#include "ecs/physics/Joints.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/Sleep.hpp"
#include "ecs/physics/Solver.hpp"
#include "ecs/physics/SweptCollision.hpp"

#include <glm/geometric.hpp>

namespace systems
{

namespace
{

/// @brief Step the position from velocity for every awake dynamic body,
/// resolving immediate collisions against the static world by a single
/// swept-AABB hit.  Penetrations from over-pushed bodies are resolved by
/// the position-correction pass in `solveContacts`.
void integrateAndCollide(Registry& registry,
                         float dt,
                         const physics::WorldGeometry& world,
                         physics::ContactCache& cache)
{
    auto view = registry.view<RigidBody, Position, Velocity, CollisionShape>();
    for (auto e : view) {
        RigidBody& rb = view.get<RigidBody>(e);
        if (rb.isAsleep || rb.invMass <= 0.0f)
            continue;

        Position& pos = view.get<Position>(e);
        Velocity& vel = view.get<Velocity>(e);
        const CollisionShape& shape = view.get<CollisionShape>(e);

        // Gravity — for now, hard-code Y-up.  Phase 7+ rigid bodies fall
        // independently of the player's gravity-flip state.
        vel.value.y -= physics::k_gravity * dt;

        // Swept-AABB against static world.  Capsule bodies use their
        // bounding AABB here (Phase 5 conservative path — exact for
        // axis-aligned face normals which dominate hand-authored maps).
        const glm::vec3 start = pos.value;
        const glm::vec3 end = start + vel.value * dt;
        const physics::HitResult hit = physics::sweepAll(shape.halfExtents, start, end, world);

        if (!hit.hit) {
            pos.value = end;
            continue;
        }

        // Move up to the contact point, reflect the velocity.
        pos.value = start + (end - start) * hit.tFirst;
        // Push slightly off the surface to avoid re-penetration next tick.
        pos.value += hit.normal * 0.03125f;

        // Build a manifold entry for the solver to handle resting / friction.
        // Single-point contact — sufficient for capsule / sphere rigid bodies
        // touching static geometry.  Box-on-box stacking would benefit from
        // multi-point clipping (Phase 9 has the data structure ready; the
        // generator is single-point for now).
        physics::ContactManifold mf;
        mf.a = entt::null; // null = the static world
        mf.b = e;
        mf.aIsStatic = true;
        mf.bIsStatic = false;
        mf.normal = -hit.normal; // points from A (world) → B (body)
        mf.pointCount = 1;
        const float radius = shape.minkowskiExtent(hit.normal);
        const glm::vec3 contactWorld = pos.value - hit.normal * radius;
        mf.points[0].worldPositionA = contactWorld;
        mf.points[0].worldPositionB = contactWorld;
        mf.points[0].localB = contactWorld - pos.value;
        mf.points[0].depth = 0.0f;
        mf.surfaceA = hit.surfaceType;
        cache.merge(mf);
    }
}

} // namespace

void runDynamics(Registry& registry,
                 float dt,
                 const physics::WorldGeometry& world,
                 physics::ContactCache& cache,
                 const physics::SolverConfig& solverCfg,
                 const physics::SleepConfig& sleepCfg)
{
    // 1 + 2: Integrate velocities and generate static-world manifolds.
    integrateAndCollide(registry, dt, world, cache);

    // 3: Solve everything.
    physics::solveContacts(registry, cache, solverCfg, dt);
    physics::solveJoints(registry, solverCfg, dt);

    // 4: Sleep state update.
    physics::updateSleep(registry, sleepCfg);

    // 5: Reap stale manifolds (pairs that didn't refresh this tick).
    cache.endFrame();
}

} // namespace systems
