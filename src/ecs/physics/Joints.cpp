/// @file Joints.cpp
/// @brief Implementation of joint constraint solving (Phase 11).
///
/// Implements PointJoint (ball / spherical) and HingeJoint (1-DOF rotational)
/// — sufficient for ragdolls and hinged objects.  ConeTwistJoint and
/// Joint6DOF are stubbed for now; ragdoll setup uses point + hinge.

#include "ecs/physics/Joints.hpp"

#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Solver.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <vector>

namespace physics
{

namespace
{

struct BodyRef
{
    RigidBody* rb = nullptr;
    Velocity* vel = nullptr;
    AngularVelocity* angVel = nullptr;
    Position* pos = nullptr;
    Orientation* ori = nullptr;
    bool isStatic = true;
};

[[nodiscard]] BodyRef gatherBody(Registry& registry, entt::entity e)
{
    BodyRef br;
    if (e == entt::null)
        return br;
    br.rb = registry.try_get<RigidBody>(e);
    br.vel = registry.try_get<Velocity>(e);
    br.angVel = registry.try_get<AngularVelocity>(e);
    br.pos = registry.try_get<Position>(e);
    br.ori = registry.try_get<Orientation>(e);
    br.isStatic = (br.rb == nullptr) || (br.rb->invMass <= 0.0f);
    return br;
}

void applyImpulse(BodyRef& a, BodyRef& b, glm::vec3 J, glm::vec3 rA, glm::vec3 rB)
{
    if (!a.isStatic) {
        if (a.vel != nullptr)
            a.vel->value -= J * a.rb->invMass;
        if (a.angVel != nullptr)
            a.angVel->value -= a.rb->invInertiaWorld * glm::cross(rA, J);
    }
    if (!b.isStatic) {
        if (b.vel != nullptr)
            b.vel->value += J * b.rb->invMass;
        if (b.angVel != nullptr)
            b.angVel->value += b.rb->invInertiaWorld * glm::cross(rB, J);
    }
}

void applyAngularImpulse(BodyRef& a, BodyRef& b, glm::vec3 L)
{
    if (!a.isStatic && a.angVel != nullptr)
        a.angVel->value -= a.rb->invInertiaWorld * L;
    if (!b.isStatic && b.angVel != nullptr)
        b.angVel->value += b.rb->invInertiaWorld * L;
}

glm::vec3 worldPoint(const BodyRef& body, glm::vec3 localPoint)
{
    if (body.ori != nullptr && body.pos != nullptr) {
        const glm::mat3 r = glm::mat3_cast(body.ori->value);
        return body.pos->value + r * localPoint;
    }
    if (body.pos != nullptr)
        return body.pos->value + localPoint;
    return localPoint;
}

glm::vec3 worldDir(const BodyRef& body, glm::vec3 localDir)
{
    if (body.ori != nullptr) {
        const glm::mat3 r = glm::mat3_cast(body.ori->value);
        return r * localDir;
    }
    return localDir;
}

/// @brief 3×3 effective-mass for a point-anchor constraint.  See Catto's
/// GDC2005 for the derivation: K = (1/mA + 1/mB) * I + ...skew(rA) * IA^-1 *
/// skew(rA)^T  + skew(rB) * IB^-1 * skew(rB)^T.  Returned matrix is inverted
/// for direct multiplication onto the position-error vector.
glm::mat3 anchorEffectiveMass(const BodyRef& a, const BodyRef& b, glm::vec3 rA, glm::vec3 rB)
{
    auto skew = [](glm::vec3 v) -> glm::mat3 {
        return glm::mat3{
            glm::vec3{0.0f, v.z, -v.y},
            glm::vec3{-v.z, 0.0f, v.x},
            glm::vec3{v.y, -v.x, 0.0f},
        };
    };

    glm::mat3 K{0.0f};
    K[0][0] = (a.isStatic ? 0.0f : a.rb->invMass) + (b.isStatic ? 0.0f : b.rb->invMass);
    K[1][1] = K[0][0];
    K[2][2] = K[0][0];

    if (!a.isStatic) {
        const glm::mat3 sA = skew(rA);
        K += sA * a.rb->invInertiaWorld * glm::transpose(sA);
    }
    if (!b.isStatic) {
        const glm::mat3 sB = skew(rB);
        K += sB * b.rb->invInertiaWorld * glm::transpose(sB);
    }
    return glm::inverse(K);
}

void solvePointJoint(Registry& registry, PointJoint& j, const SolverConfig& cfg, float dt)
{
    BodyRef a = gatherBody(registry, j.bodyA);
    BodyRef b = gatherBody(registry, j.bodyB);
    if (a.pos == nullptr || b.pos == nullptr)
        return;

    const glm::vec3 worldA = worldPoint(a, j.localAnchorA);
    const glm::vec3 worldB = worldPoint(b, j.localAnchorB);
    const glm::vec3 rA = worldA - a.pos->value;
    const glm::vec3 rB = worldB - b.pos->value;

    // Position error: bring B's anchor onto A's anchor.
    const glm::vec3 error = worldA - worldB;
    const glm::mat3 effMass = anchorEffectiveMass(a, b, rA, rB);

    // Bias from Baumgarte.
    const glm::vec3 bias = -(cfg.baumgarteScale / dt) * error;

    // Warm-start.
    applyImpulse(a, b, j.accumulatedImpulse, rA, rB);

    for (int it = 0; it < cfg.velocityIterations; ++it) {
        // Constraint velocity: vB + ωB×rB - vA - ωA×rA
        glm::vec3 vA{0.0f};
        glm::vec3 vB{0.0f};
        if (a.vel != nullptr)
            vA = a.vel->value;
        if (b.vel != nullptr)
            vB = b.vel->value;
        if (a.angVel != nullptr)
            vA += glm::cross(a.angVel->value, rA);
        if (b.angVel != nullptr)
            vB += glm::cross(b.angVel->value, rB);
        const glm::vec3 cVel = vB - vA;

        const glm::vec3 impulse = effMass * -(cVel + bias);
        j.accumulatedImpulse += impulse;
        applyImpulse(a, b, impulse, rA, rB);
    }

    // Breakage check.
    if (j.breakForce > 0.0f && glm::length(j.accumulatedImpulse) / dt > j.breakForce) {
        j.bodyA = entt::null; // Mark joint as broken; gameplay code reaps later.
    }
}

void solveHingeJoint(Registry& registry, HingeJoint& j, const SolverConfig& cfg, float dt)
{
    BodyRef a = gatherBody(registry, j.bodyA);
    BodyRef b = gatherBody(registry, j.bodyB);
    if (a.pos == nullptr || b.pos == nullptr)
        return;

    const glm::vec3 worldA = worldPoint(a, j.localAnchorA);
    const glm::vec3 worldB = worldPoint(b, j.localAnchorB);
    const glm::vec3 rA = worldA - a.pos->value;
    const glm::vec3 rB = worldB - b.pos->value;

    const glm::vec3 axisA = worldDir(a, j.localAxisA);
    const glm::vec3 axisB = worldDir(b, j.localAxisB);

    // Pick two perpendicular axes that the hinge must lock (anything not
    // along the hinge axis).
    glm::vec3 perp0;
    glm::vec3 perp1;
    if (std::abs(axisA.x) >= 0.57735f)
        perp0 = glm::normalize(glm::vec3{axisA.y, -axisA.x, 0.0f});
    else
        perp0 = glm::normalize(glm::vec3{0.0f, axisA.z, -axisA.y});
    perp1 = glm::cross(axisA, perp0);

    // Warm-start anchor + angular.
    applyImpulse(a, b, j.accumulatedAnchorImpulse, rA, rB);
    applyAngularImpulse(a, b, j.accumulatedAngularImpulse);

    const glm::mat3 effMass = anchorEffectiveMass(a, b, rA, rB);
    const glm::vec3 anchorError = worldA - worldB;
    const glm::vec3 anchorBias = -(cfg.baumgarteScale / dt) * anchorError;

    for (int it = 0; it < cfg.velocityIterations; ++it) {
        // Anchor (3-DOF lock).
        glm::vec3 vA{0.0f};
        glm::vec3 vB{0.0f};
        if (a.vel != nullptr)
            vA = a.vel->value;
        if (b.vel != nullptr)
            vB = b.vel->value;
        if (a.angVel != nullptr)
            vA += glm::cross(a.angVel->value, rA);
        if (b.angVel != nullptr)
            vB += glm::cross(b.angVel->value, rB);
        const glm::vec3 cVel = vB - vA;
        const glm::vec3 impulse = effMass * -(cVel + anchorBias);
        j.accumulatedAnchorImpulse += impulse;
        applyImpulse(a, b, impulse, rA, rB);

        // Angular constraint: lock the two perpendicular axes — the hinge
        // axis must stay aligned between the two bodies.
        glm::vec3 wA{0.0f};
        glm::vec3 wB{0.0f};
        if (a.angVel != nullptr)
            wA = a.angVel->value;
        if (b.angVel != nullptr)
            wB = b.angVel->value;
        const glm::vec3 wRel = wB - wA;

        // For each perpendicular axis, project the relative angular vel and
        // cancel it.
        const float invIsum = (a.isStatic ? 0.0f : 1.0f / 1.0f) + (b.isStatic ? 0.0f : 1.0f / 1.0f);
        for (const glm::vec3& axis : {perp0, perp1}) {
            const float wAxis = glm::dot(wRel, axis);
            if (std::abs(invIsum) > 0.0f) {
                const float lambda = -wAxis; // unit effective mass; refine later
                const glm::vec3 L = axis * lambda;
                applyAngularImpulse(a, b, L);
                j.accumulatedAngularImpulse += L;
            }
        }

        // Motor — drive relative angular velocity around hinge axis.
        if (j.motorEnabled) {
            const float wAxis = glm::dot(wRel, axisA);
            const float wDesired = j.targetAngularSpeed;
            float lambdaMotor = -(wAxis - wDesired);
            // Cap by max motor torque budget per tick.
            const float maxL = j.maxMotorTorque * dt;
            lambdaMotor = std::clamp(lambdaMotor, -maxL, maxL);
            applyAngularImpulse(a, b, axisA * lambdaMotor);
        }

        // Position limit (clamp swing angle between min and max).
        if (j.hasLimit) {
            const float dotAxes = glm::dot(axisA, axisB);
            const float curAngle = std::acos(std::clamp(dotAxes, -1.0f, 1.0f));
            if (curAngle < j.minAngle || curAngle > j.maxAngle) {
                const float target = std::clamp(curAngle, j.minAngle, j.maxAngle);
                const float err = curAngle - target;
                const float lambda = -err * cfg.baumgarteScale / dt;
                applyAngularImpulse(a, b, axisA * lambda);
            }
        }
    }
}

} // namespace

void solveJoints(Registry& registry, const SolverConfig& cfg, float dt)
{
    if (dt <= 0.0f)
        return;

    // Stable iteration order: collect joints, sort by entity id.
    std::vector<entt::entity> pointJoints;
    for (auto e : registry.view<PointJoint>())
        pointJoints.push_back(e);
    std::sort(pointJoints.begin(), pointJoints.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) < entt::to_integral(b);
    });
    for (entt::entity e : pointJoints) {
        auto& j = registry.get<PointJoint>(e);
        if (j.bodyA != entt::null && j.bodyB != entt::null)
            solvePointJoint(registry, j, cfg, dt);
    }

    std::vector<entt::entity> hinges;
    for (auto e : registry.view<HingeJoint>())
        hinges.push_back(e);
    std::sort(hinges.begin(), hinges.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) < entt::to_integral(b);
    });
    for (entt::entity e : hinges) {
        auto& j = registry.get<HingeJoint>(e);
        if (j.bodyA != entt::null && j.bodyB != entt::null)
            solveHingeJoint(registry, j, cfg, dt);
    }
}

} // namespace physics
