/// @file RigidBody.hpp
/// @brief Dynamic-body state for the Phase 6+ physics core.
///
/// Phase 6 establishes the force/impulse API on top of a minimal RigidBody.
/// Phase 7 will expand this with mass / inertia tensor / angular velocity /
/// torque for full 6-DOF dynamics.  Entities without a RigidBody are
/// treated as unit-mass kinematic bodies — the legacy direct-velocity
/// mutation pattern continues to work transparently.

#pragma once

#include <cstdint>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

/// @brief Dynamic-body state shared across the force, impulse, and (Phase 7+)
/// constraint-solver paths.
///
/// **Lifecycle.** Accumulators (`forceAccum`, `impulseAccum`,
/// `torqueAccum`, `angImpulseAccum`) are written by `forces::apply*`
/// helpers throughout a tick, drained into `velocity` / `angularVelocity`
/// by `forces::integrateAccumulators` at tick start, and then cleared.
struct RigidBody
{
    /// @brief Inverse mass.  0 = static / kinematic (infinite mass — applied
    /// forces have no effect).  Defaults to 1 / 80 kg ≈ 0.0125 — a typical
    /// human-sized character mass.
    float invMass = 1.0f / 80.0f;

    /// @brief Local-space inverse inertia tensor.  Diagonal-only suffices for
    /// the box / capsule / sphere shapes we currently support; off-diagonal
    /// terms are zero.  Phase 7 will refresh `invInertiaWorld` each frame
    /// from this and the body's orientation.
    glm::mat3 localInvInertia{0.0f};

    /// @brief World-space inverse inertia tensor.  Computed each tick by
    /// Phase 7's integration step as `R * localInvInertia * R^T` (where R is
    /// the body's rotation matrix).  Phase 6 leaves this at identity since
    /// no rotation is integrated yet.
    glm::mat3 invInertiaWorld{0.0f};

    /// @brief Continuous force accumulator.  Integrated as `dv += F * dt * invMass`.
    glm::vec3 forceAccum{0.0f};

    /// @brief Instantaneous impulse accumulator.  Integrated as `dv += J * invMass`
    /// (no `dt` — impulses are pre-integrated forces).
    glm::vec3 impulseAccum{0.0f};

    /// @brief Continuous torque accumulator (Phase 7).
    glm::vec3 torqueAccum{0.0f};

    /// @brief Instantaneous angular-impulse accumulator (Phase 7).
    glm::vec3 angImpulseAccum{0.0f};

    /// @brief Per-frame linear damping factor.  velocity *= (1 - damping*dt).
    /// 0 = no damping.  Useful for atmospheric drag on projectiles.
    float linearDamping = 0.0f;

    /// @brief Per-frame angular damping factor (Phase 7).
    float angularDamping = 0.05f;

    /// @brief Sleep state (Phase 12).  Sleeping bodies skip integration.
    bool isAsleep = false;

    /// @brief Frames since the body's energy dropped below the sleep
    /// threshold (Phase 12).
    uint16_t sleepCounter = 0;
};
