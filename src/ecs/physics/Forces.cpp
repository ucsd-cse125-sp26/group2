/// @file Forces.cpp
/// @brief Implementation of the force / impulse / torque API.

#include "ecs/physics/Forces.hpp"

#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

namespace physics::forces
{

void applyImpulse(Registry& registry, entt::entity entity, glm::vec3 impulse) noexcept
{
    if (auto* rb = registry.try_get<RigidBody>(entity)) {
        rb->impulseAccum += impulse;
        return;
    }
    // No RigidBody: legacy unit-mass kinematic semantics.
    if (auto* vel = registry.try_get<Velocity>(entity))
        vel->value += impulse;
}

void applyForce(Registry& registry, entt::entity entity, glm::vec3 force) noexcept
{
    if (auto* rb = registry.try_get<RigidBody>(entity)) {
        rb->forceAccum += force;
        return;
    }
    // No RigidBody: forces have no effect on legacy kinematic entities — they
    // need the integrating step.  Treat as if dt=0; callers that want a
    // legacy-compatible mutation should use `applyImpulse` instead.
}

void applyImpulseAtPoint(Registry& registry, entt::entity entity, glm::vec3 impulse, glm::vec3 worldPoint) noexcept
{
    // Linear component unchanged.
    applyImpulse(registry, entity, impulse);

    // Angular component: lever arm × J  (only when we have a RigidBody and a Position).
    auto* rb = registry.try_get<RigidBody>(entity);
    auto* pos = registry.try_get<Position>(entity);
    if (rb == nullptr || pos == nullptr)
        return;
    const glm::vec3 r = worldPoint - pos->value;
    rb->angImpulseAccum += glm::cross(r, impulse);
}

void applyForceAtPoint(Registry& registry, entt::entity entity, glm::vec3 force, glm::vec3 worldPoint) noexcept
{
    applyForce(registry, entity, force);

    auto* rb = registry.try_get<RigidBody>(entity);
    auto* pos = registry.try_get<Position>(entity);
    if (rb == nullptr || pos == nullptr)
        return;
    const glm::vec3 r = worldPoint - pos->value;
    rb->torqueAccum += glm::cross(r, force);
}

void applyTorque(Registry& registry, entt::entity entity, glm::vec3 torque) noexcept
{
    if (auto* rb = registry.try_get<RigidBody>(entity))
        rb->torqueAccum += torque;
}

void integrateAccumulators(Registry& registry, float dt) noexcept
{
    // Drain accumulators into velocities.  Iterated in a stable order
    // (entt's view iteration is archetype-stable per registry contents)
    // so the integration is deterministic.
    auto view = registry.view<RigidBody, Velocity>();
    for (auto e : view) {
        RigidBody& rb = view.get<RigidBody>(e);
        if (rb.isAsleep)
            continue;

        Velocity& vel = view.get<Velocity>(e);

        // Linear: dv = (F * dt + J) * invMass
        if (rb.invMass > 0.0f) {
            const glm::vec3 dv = (rb.forceAccum * dt + rb.impulseAccum) * rb.invMass;
            vel.value += dv;
            if (rb.linearDamping > 0.0f)
                vel.value *= std::max(0.0f, 1.0f - rb.linearDamping * dt);
        }

        // Phase 7 — angular integration.  Requires an Orientation +
        // AngularVelocity pair on the entity.  Without them we can't apply
        // torque (the local→world inertia transform isn't defined), so just
        // clear the accumulators.
        auto* ori = registry.try_get<Orientation>(e);
        auto* angVel = registry.try_get<AngularVelocity>(e);
        if (ori != nullptr && angVel != nullptr) {
            // Refresh the world-space inverse inertia: R * I_local * R^T.
            const glm::mat3 r = glm::mat3_cast(ori->value);
            const glm::mat3 rt = glm::transpose(r);
            rb.invInertiaWorld = r * rb.localInvInertia * rt;

            // dω = I^-1 (τ * dt + L)
            const glm::vec3 dOmega = rb.invInertiaWorld * (rb.torqueAccum * dt + rb.angImpulseAccum);
            angVel->value += dOmega;
            if (rb.angularDamping > 0.0f)
                angVel->value *= std::max(0.0f, 1.0f - rb.angularDamping * dt);

            // Integrate orientation: q_new = q + 0.5 dt (0, ω) ⊗ q,
            // then renormalise.
            const glm::quat omegaQuat{0.0f, angVel->value.x, angVel->value.y, angVel->value.z};
            ori->value = glm::normalize(ori->value + 0.5f * dt * (omegaQuat * ori->value));
        }

        rb.forceAccum = glm::vec3{0.0f};
        rb.impulseAccum = glm::vec3{0.0f};
        rb.torqueAccum = glm::vec3{0.0f};
        rb.angImpulseAccum = glm::vec3{0.0f};
    }
}

} // namespace physics::forces
