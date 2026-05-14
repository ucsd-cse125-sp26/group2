/// @file Forces.hpp
/// @brief Unified force / impulse / torque API.
///
/// Replaces the older pattern of directly mutating a `Velocity` component
/// from gameplay code (knockback, projectile bounce, explosion) with a
/// proper accumulator path that respects per-body mass / inertia / damping.
///
/// **Compatibility.** Entities without a `RigidBody` fall back to "unit
/// mass kinematic" semantics — `applyImpulse` becomes `velocity += impulse`
/// exactly as the legacy code did.  Phase 7 turns the player into a real
/// rigid body with mass-aware integration; until then, the API is just a
/// thin abstraction over the existing mutation pattern.

#pragma once

#include "ecs/registry/Registry.hpp"

#include <glm/vec3.hpp>

namespace physics::forces
{

/// @brief Apply an instantaneous impulse (units: kg·m/s) at the entity's
/// centre-of-mass.  No torque component.
///
/// If the entity has `RigidBody`, the impulse accumulates into
/// `impulseAccum` and is integrated as `velocity += J * invMass` at tick
/// start.  Otherwise, falls back to `velocity += impulse` directly.
///
/// Thread-safe iff the same `entity` is not modified from multiple threads
/// in the same tick.
void applyImpulse(Registry& registry, entt::entity entity, glm::vec3 impulse) noexcept;

/// @brief Apply a continuous force (units: kg·m/s²) at the entity's centre.
/// Integrated as `velocity += F * dt * invMass`.
void applyForce(Registry& registry, entt::entity entity, glm::vec3 force) noexcept;

/// @brief Apply an off-centre impulse.  The linear component changes
/// `velocity`; the angular component (lever arm × J) changes
/// `angularVelocity`.  Linear-only fallback for entities without a
/// RigidBody (no rotation yet).
///
/// @param worldPoint  World-space point where the impulse is applied.
void applyImpulseAtPoint(
    Registry& registry, entt::entity entity, glm::vec3 impulse, glm::vec3 worldPoint) noexcept;

/// @brief Apply an off-centre continuous force.  Same semantics as
/// `applyImpulseAtPoint` but as a force, integrated by `dt`.
void applyForceAtPoint(
    Registry& registry, entt::entity entity, glm::vec3 force, glm::vec3 worldPoint) noexcept;

/// @brief Apply a torque (Phase 7).  No-op for entities without a RigidBody.
void applyTorque(Registry& registry, entt::entity entity, glm::vec3 torque) noexcept;

/// @brief Drain every entity's force / impulse accumulators into its
/// velocity (and, in Phase 7, angular velocity).  Called once per physics
/// tick from `runMovement` before the integration step.
///
/// @param registry  ECS registry.
/// @param dt        Tick duration (seconds).
void integrateAccumulators(Registry& registry, float dt) noexcept;

} // namespace physics::forces
