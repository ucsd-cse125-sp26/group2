/// @file Solver.hpp
/// @brief Sequential-Impulse (PGS) constraint solver for contacts + joints.
///
/// Reference: Erin Catto, *Iterative Dynamics with Temporal Coherence* (GDC
/// 2005) — the canonical formulation that Box2D / Bullet use.  Per-tick:
///   1. Build `ContactConstraint` rows from every cached `ContactManifold`.
///   2. Warm-start: apply each cached impulse from the previous tick.
///   3. Iterate ~8 times: per contact, compute relative velocity at the
///      contact point, derive a normal impulse `λN`, clamp accumulated
///      impulse ≥ 0, apply to both bodies.  Then the friction tangents
///      with Coulomb clamping `|λT| ≤ μ * λN`.
///   4. Position correction (optional / NGS) for residual penetration.

#pragma once

#include "ecs/physics/ContactCache.hpp"
#include "ecs/registry/Registry.hpp"

#include <cstdint>

namespace physics
{

struct SolverConfig
{
    int positionIterations = 3;           ///< NGS passes to push out residual penetration.
    int velocityIterations = 8;           ///< PGS passes for impulse solving.
    float baumgarteScale = 0.2f;          ///< Position-bias factor for Baumgarte stabilisation.
    float linearSlop = 0.005f;            ///< Allowed penetration before bias activates (units).
    float maxLinearCorrection = 0.2f;     ///< Cap per-iter NGS push to avoid jitter.
    float defaultFriction = 0.7f;         ///< Coulomb friction coefficient if not on surface table.
    float defaultRestitution = 0.0f;      ///< Bounce coefficient.
    float velThreshForRestitution = 1.0f; ///< Below this, restitution is suppressed (resting contact).
};

/// @brief Solve every cached contact manifold for the current tick.
/// Bodies' Velocity / AngularVelocity components are updated in place.
///
/// Caller responsibilities:
///   1. `ContactCache::endFrame()` after this returns (cleans stale manifolds).
///   2. Re-extract manifolds back into the cache so accumulated impulses
///      persist (already handled by Solver internally — it writes back
///      after each iteration).
void solveContacts(Registry& registry, ContactCache& cache, const SolverConfig& cfg, float dt);

} // namespace physics
