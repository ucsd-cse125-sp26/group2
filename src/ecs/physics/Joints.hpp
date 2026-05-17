/// @file Joints.hpp
/// @brief Constraint definitions for point, hinge, cone-twist, and 6-DOF joints.
///
/// Each joint is stored as an ECS component on a "joint entity" — the
/// joint entity itself isn't a body, just a constraint between two bodies
/// `bodyA` and `bodyB`.  Joints feed into the Phase 10 PGS solver via
/// `solveJoints()` which runs alongside `solveContacts()`.
///
/// **Anchor frames.** Each side holds the joint anchor in the *body's
/// local space* so the world-space anchors track the bodies as they
/// translate / rotate.  Setting up a joint at construction time computes
/// these local frames from the current world transforms.
///
/// **Limits + motors.** Phase-11 ships position limits (clamp) and
/// velocity motors (maxForce-capped impulse driver) — sufficient for
/// ragdolls, doors, hinged platforms.

#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace physics
{

/// @brief Spherical/ball joint — locks the world-space anchor points of
/// two bodies together while permitting arbitrary rotation.
struct PointJoint
{
    entt::entity bodyA{entt::null};
    entt::entity bodyB{entt::null};
    glm::vec3 localAnchorA{0.0f};       ///< Anchor in body A's local frame.
    glm::vec3 localAnchorB{0.0f};       ///< Anchor in body B's local frame.
    glm::vec3 accumulatedImpulse{0.0f}; ///< Warm-started across frames.
    float breakForce = 0.0f;            ///< If > 0, joint detaches when impulse exceeds.
};

/// @brief Single-axis hinge — locks the bodies' anchors together AND
/// constrains their relative rotation to a single axis.  Optional swing
/// limit (relative to body A's local-space rest orientation).
struct HingeJoint
{
    entt::entity bodyA{entt::null};
    entt::entity bodyB{entt::null};
    glm::vec3 localAnchorA{0.0f};
    glm::vec3 localAnchorB{0.0f};
    glm::vec3 localAxisA{0.0f, 0.0f, 1.0f}; ///< Hinge axis in A's local space.
    glm::vec3 localAxisB{0.0f, 0.0f, 1.0f}; ///< Hinge axis in B's local space.

    /// @brief Limit angles in radians.  When `minAngle == maxAngle`, the
    /// joint is locked; when both are zero, no limit is applied.
    float minAngle = 0.0f;
    float maxAngle = 0.0f;
    bool hasLimit = false;

    /// @brief Motor — if `enabled`, drive `angularVelocity` toward
    /// `targetAngularSpeed` with at most `maxMotorTorque`.
    bool motorEnabled = false;
    float targetAngularSpeed = 0.0f;
    float maxMotorTorque = 0.0f;

    glm::vec3 accumulatedAnchorImpulse{0.0f};
    glm::vec3 accumulatedAngularImpulse{0.0f};
};

/// @brief Cone-twist joint — locks anchors AND constrains relative
/// rotation to a swing-cone + twist-limit.  Standard ragdoll shoulder /
/// hip joint.
struct ConeTwistJoint
{
    entt::entity bodyA{entt::null};
    entt::entity bodyB{entt::null};
    glm::vec3 localAnchorA{0.0f};
    glm::vec3 localAnchorB{0.0f};
    glm::quat localFrameA{1.0f, 0.0f, 0.0f, 0.0f}; ///< Rest orientation in A's space.
    glm::quat localFrameB{1.0f, 0.0f, 0.0f, 0.0f};

    float swingLimit = 0.6f; ///< Max half-cone angle, radians (~35°).
    float twistLimit = 0.5f; ///< Max twist about cone axis, radians.

    glm::vec3 accumulatedAnchorImpulse{0.0f};
    glm::vec3 accumulatedAngularImpulse{0.0f};
};

/// @brief Generic 6-DOF joint — per-axis lock / limit / free + optional
/// motor and spring on each axis.  Phase 11+ feature, currently a marker
/// component; full implementation deferred to per-need.
struct Joint6DOF
{
    entt::entity bodyA{entt::null};
    entt::entity bodyB{entt::null};
    glm::vec3 localAnchorA{0.0f};
    glm::vec3 localAnchorB{0.0f};

    enum class AxisMode : uint8_t
    {
        Free,    ///< No constraint
        Limited, ///< Clamp to [min, max]
        Locked,  ///< minAngle == maxAngle
    };

    AxisMode linearMode[3] = {AxisMode::Locked, AxisMode::Locked, AxisMode::Locked};
    AxisMode angularMode[3] = {AxisMode::Locked, AxisMode::Locked, AxisMode::Locked};
    glm::vec3 linearMin{0.0f};
    glm::vec3 linearMax{0.0f};
    glm::vec3 angularMin{0.0f};
    glm::vec3 angularMax{0.0f};
};

struct SolverConfig;

} // namespace physics

#include "ecs/registry/Registry.hpp"

namespace physics
{

/// @brief Solve every joint in the registry using sequential impulses.
/// Called from the same physics step as `solveContacts`.  Joints are
/// iterated in stable order (sorted by entity id) for determinism.
void solveJoints(Registry& registry, const SolverConfig& cfg, float dt);

} // namespace physics
