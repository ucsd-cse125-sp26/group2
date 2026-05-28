/// @file RagdollPbd.hpp
/// @brief Position-Based-Dynamics ragdoll constraint system.
///
/// Replaces the velocity-bias (PGS+Baumgarte) joint solver for ragdoll
/// bodies with a hard position-projection approach inspired by CS2 /
/// Source 2 articulated ragdolls and the Müller PBD paper. The
/// connectivity invariant — every parent-child anchor coincides in world
/// space — is enforced *directly* by translating bodies, not by injecting
/// corrective velocity into the next tick's solve. Bones therefore CANNOT
/// detach under any forcing: gravity, contact impulses, knockback, etc.
/// can only bend the joints (within angular limits) — never break them.
///
/// Per-tick order inside `runDynamics`:
///   1. Integrate velocity (gravity).
///   2. Sweep against static world → contact manifolds.
///   3. PGS contact solver (impulses against the world only).
///   4. `enforceRagdollConnectivity` — N PBD passes per joint, position
///      projection then angular-limit projection.
///   5. Velocity clamp + sleep.
///
/// Why this is more stable than the previous PGS joint pass:
///   - Position error is corrected by moving bodies; the solver can't
///     "overshoot" because the gap closes exactly each iteration.
///   - Mass ratios still matter (heavier bodies move less) but no longer
///     control whether the constraint converges at all.
///   - Angular limits are clamped, not biased toward — chest-twisting
///     past 90° is impossible by construction.

#pragma once

#include "ecs/registry/Registry.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace physics
{

/// @brief One articulated joint in a PBD ragdoll skeleton.
///
/// Lifetime: one entity per joint, holds this single component.
/// `bodyA` is the parent, `bodyB` the child. Anchors `localAnchorA` /
/// `localAnchorB` are the joint origin in each body's local space — they
/// MUST coincide in world space at all times (PBD enforces this).
struct RagdollPbdJoint
{
    enum class Kind : uint8_t
    {
        Point,    ///< Ball-and-socket. Cone limit applied (`swingLimit`).
        Hinge,    ///< 1-DOF rotation about `axisLocalA`. Clamped to [`hingeMin`, `hingeMax`].
        ConeTwist ///< Swing limit (cone) + twist limit about `axisLocalA`.
    };

    entt::entity bodyA{entt::null}; ///< Parent.
    entt::entity bodyB{entt::null}; ///< Child.

    glm::vec3 localAnchorA{0.0f}; ///< Joint origin in parent local space.
    glm::vec3 localAnchorB{0.0f}; ///< Joint origin in child local space.

    Kind kind = Kind::Point;

    /// @brief Hinge / twist axis expressed in the parent's local frame.
    glm::vec3 axisLocalA{1.0f, 0.0f, 0.0f};

    /// @brief Max half-cone angle for swing about the joint frame (radians).
    /// `Point` and `ConeTwist` consult this; `Hinge` ignores it.
    float swingLimit = 1.4f;

    /// @brief Max ± twist angle for `ConeTwist` (radians).
    float twistLimit = 0.8f;

    /// @brief Hinge angle bounds (radians). Used only when `kind == Hinge`.
    float hingeMin = -1.5f;
    float hingeMax = 0.05f;
};

/// @brief Enforce ragdoll connectivity + angular limits via N PBD iterations.
///
/// Each iteration walks every `RagdollPbdJoint` in deterministic order,
/// computes the world-space anchor error, and translates the two bodies
/// to close the gap (split by inverse mass). Then a separate pass clamps
/// the relative rotation between parent and child into the joint's
/// angular limit. After all iterations, per-body velocities are derived
/// from the net position change (PBD-style) so contact response in the
/// next tick still sees realistic motion.
///
/// Typical configuration: 8 iterations per tick is overkill for a
/// 15-body humanoid; 4 already gives sub-millimetre anchor error.
void enforceRagdollConnectivity(Registry& registry, float dt, int iterations = 8);

} // namespace physics
