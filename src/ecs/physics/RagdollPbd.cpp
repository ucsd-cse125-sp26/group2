/// @file RagdollPbd.cpp
/// @brief Position-Based-Dynamics ragdoll constraint enforcement.

#include "ecs/physics/RagdollPbd.hpp"

#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace physics
{

namespace
{

/// @brief Build a per-iteration record of the body state we touch, so we
/// avoid hammering try_get every joint pass.
struct BodyRef
{
    Position* pos = nullptr;
    Orientation* ori = nullptr;
    Velocity* vel = nullptr;
    AngularVelocity* angVel = nullptr;
    const RigidBody* rb = nullptr;
    glm::vec3 originalPos{0.0f};
    glm::quat originalOri{1.0f, 0.0f, 0.0f, 0.0f};
    float invMass = 0.0f;
    bool valid = false;
};

BodyRef gather(Registry& registry, entt::entity e)
{
    BodyRef ref;
    if (e == entt::null || !registry.valid(e))
        return ref;
    ref.pos = registry.try_get<Position>(e);
    ref.ori = registry.try_get<Orientation>(e);
    ref.vel = registry.try_get<Velocity>(e);
    ref.angVel = registry.try_get<AngularVelocity>(e);
    ref.rb = registry.try_get<RigidBody>(e);
    if (ref.pos == nullptr || ref.rb == nullptr)
        return ref;
    ref.originalPos = ref.pos->value;
    ref.originalOri = (ref.ori != nullptr) ? ref.ori->value : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    ref.invMass = ref.rb->invMass;
    ref.valid = true;
    return ref;
}

/// @brief Canonical (w ≥ 0) form so we can read off axis-angle without
/// branching on the dual covering.
glm::quat canonical(glm::quat q)
{
    if (q.w < 0.0f)
        return -q;
    return q;
}

/// @brief Project a quaternion onto rotation about a specific axis (Bullet's
/// classical swing-twist split). Returns (swing, twist) such that
/// `q = swing * twist`, twist's rotation axis is `axis`.
void swingTwistDecompose(glm::quat q, glm::vec3 axis, glm::quat& outSwing, glm::quat& outTwist)
{
    const glm::vec3 r{q.x, q.y, q.z};
    const float d = glm::dot(r, axis);
    const glm::vec3 p = axis * d;
    glm::quat twist{q.w, p.x, p.y, p.z};
    const float n2 = twist.w * twist.w + twist.x * twist.x + twist.y * twist.y + twist.z * twist.z;
    if (n2 < 1e-10f) {
        outTwist = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    } else {
        const float invN = 1.0f / std::sqrt(n2);
        twist.w *= invN;
        twist.x *= invN;
        twist.y *= invN;
        twist.z *= invN;
        outTwist = twist;
    }
    outSwing = q * glm::conjugate(outTwist);
}

/// @brief Clamp a unit quaternion's rotation magnitude to `maxAngle`
/// radians while keeping its rotation axis. Works for swing (cone) limits.
glm::quat clampAngle(glm::quat q, float maxAngle)
{
    if (maxAngle <= 0.0f)
        return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    q = canonical(q);
    const float w = std::clamp(q.w, -1.0f, 1.0f);
    const float angle = 2.0f * std::acos(w);
    if (angle <= maxAngle)
        return q;
    const float sinHalf = std::sqrt(std::max(0.0f, 1.0f - w * w));
    if (sinHalf < 1e-6f)
        return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 axis{q.x / sinHalf, q.y / sinHalf, q.z / sinHalf};
    const float halfNew = 0.5f * maxAngle;
    const float s = std::sin(halfNew);
    return glm::quat{std::cos(halfNew), axis.x * s, axis.y * s, axis.z * s};
}

/// @brief Clamp the signed twist about `axisLocal` so its angle stays
/// within `[lo, hi]` (radians). Returns the corrected twist quaternion.
glm::quat clampTwist(glm::quat twist, glm::vec3 axisLocal, float lo, float hi)
{
    twist = canonical(twist);
    const float w = std::clamp(twist.w, -1.0f, 1.0f);
    float angle = 2.0f * std::acos(w);
    const glm::vec3 axisInQuat{twist.x, twist.y, twist.z};
    const float s = glm::length(axisInQuat);
    float signedAngle = angle;
    if (s > 1e-6f) {
        const glm::vec3 unit = axisInQuat / s;
        const float dotSign = glm::dot(unit, axisLocal);
        if (dotSign < 0.0f)
            signedAngle = -angle;
    }
    const float clamped = std::clamp(signedAngle, lo, hi);
    if (clamped == signedAngle)
        return twist;
    const float halfNew = 0.5f * clamped;
    const float sn = std::sin(halfNew);
    return glm::quat{std::cos(halfNew), axisLocal.x * sn, axisLocal.y * sn, axisLocal.z * sn};
}

/// @brief Apply the position-projection constraint: translate bodies so
/// the world-space joint anchors coincide. Split correction by inverse
/// mass — a heavier body moves less.
void projectAnchor(const RagdollPbdJoint& j, BodyRef& a, BodyRef& b)
{
    if (!a.valid && !b.valid)
        return;
    const glm::vec3 worldA = a.pos->value + a.originalOri * j.localAnchorA;
    const glm::vec3 worldB = b.pos->value + b.originalOri * j.localAnchorB;
    const glm::vec3 error = worldA - worldB;
    const float wA = a.valid ? a.invMass : 0.0f;
    const float wB = b.valid ? b.invMass : 0.0f;
    const float wSum = wA + wB;
    if (wSum < 1e-9f)
        return;
    const glm::vec3 correctionA = -error * (wA / wSum);
    const glm::vec3 correctionB = error * (wB / wSum);
    if (a.valid && a.invMass > 0.0f)
        a.pos->value += correctionA;
    if (b.valid && b.invMass > 0.0f)
        b.pos->value += correctionB;
}

/// @brief Clamp the child's relative orientation into the joint's angular
/// limit. Writes back to the child's Orientation. No-op when either body
/// has no Orientation component (kinematic skeletons).
void projectAngularLimit(const RagdollPbdJoint& j, BodyRef& a, BodyRef& b)
{
    if (!a.valid || !b.valid || a.ori == nullptr || b.ori == nullptr)
        return;

    const glm::quat qA = a.ori->value;
    const glm::quat qB = b.ori->value;
    glm::quat qRel = glm::inverse(qA) * qB;

    switch (j.kind) {
    case RagdollPbdJoint::Kind::Point: {
        // Ball-and-socket: cone limit on the relative rotation magnitude.
        const glm::quat clamped = clampAngle(qRel, j.swingLimit);
        b.ori->value = qA * clamped;
        break;
    }
    case RagdollPbdJoint::Kind::Hinge: {
        // Decompose into swing (perpendicular to hinge) + twist (about
        // hinge). Force swing to identity (1-DOF), clamp twist to range.
        glm::quat swing;
        glm::quat twist;
        swingTwistDecompose(qRel, j.axisLocalA, swing, twist);
        twist = clampTwist(twist, j.axisLocalA, j.hingeMin, j.hingeMax);
        b.ori->value = qA * twist;
        break;
    }
    case RagdollPbdJoint::Kind::ConeTwist: {
        glm::quat swing;
        glm::quat twist;
        swingTwistDecompose(qRel, j.axisLocalA, swing, twist);
        swing = clampAngle(swing, j.swingLimit);
        twist = clampTwist(twist, j.axisLocalA, -j.twistLimit, j.twistLimit);
        b.ori->value = qA * swing * twist;
        break;
    }
    }
}

} // namespace

void enforceRagdollConnectivity(Registry& registry, float dt, int iterations)
{
    if (dt <= 0.0f || iterations <= 0)
        return;

    // Deterministic order: snapshot joint entities, sort by id.
    std::vector<entt::entity> jointEntities;
    for (auto e : registry.view<RagdollPbdJoint>())
        jointEntities.push_back(e);
    std::sort(jointEntities.begin(), jointEntities.end(), [](entt::entity a, entt::entity b) {
        return entt::to_integral(a) < entt::to_integral(b);
    });

    if (jointEntities.empty())
        return;

    // Snapshot every involved body's pre-projection state so we can derive
    // PBD velocity at the end of the pass. We use a flat vector keyed by
    // entity hash; for the 15-body humanoid the array stays small.
    struct BodyState
    {
        entt::entity entity{entt::null};
        glm::vec3 originalPos{0.0f};
        glm::quat originalOri{1.0f, 0.0f, 0.0f, 0.0f};
    };
    std::vector<BodyState> snapshots;
    snapshots.reserve(jointEntities.size() * 2);
    auto findOrInsert = [&](entt::entity e) -> BodyState& {
        for (auto& s : snapshots) {
            if (s.entity == e)
                return s;
        }
        BodyState s;
        s.entity = e;
        if (const auto* p = registry.try_get<Position>(e))
            s.originalPos = p->value;
        if (const auto* o = registry.try_get<Orientation>(e))
            s.originalOri = o->value;
        snapshots.push_back(s);
        return snapshots.back();
    };
    for (entt::entity je : jointEntities) {
        const auto& j = registry.get<RagdollPbdJoint>(je);
        findOrInsert(j.bodyA);
        findOrInsert(j.bodyB);
    }

    // PBD iterations: per joint, project the anchor distance to zero, then
    // clamp the relative orientation into the joint's angular limit.
    for (int iter = 0; iter < iterations; ++iter) {
        for (entt::entity je : jointEntities) {
            const auto& j = registry.get<RagdollPbdJoint>(je);
            BodyRef a = gather(registry, j.bodyA);
            BodyRef b = gather(registry, j.bodyB);
            projectAnchor(j, a, b);
            projectAngularLimit(j, a, b);
        }
    }

    // Derive velocity from the net position/orientation delta so the next
    // tick's integrator sees realistic motion (PBD convention).
    const float invDt = 1.0f / dt;
    for (const BodyState& s : snapshots) {
        if (s.entity == entt::null || !registry.valid(s.entity))
            continue;
        if (auto* vel = registry.try_get<Velocity>(s.entity)) {
            if (auto* pos = registry.try_get<Position>(s.entity)) {
                const glm::vec3 newVel = (pos->value - s.originalPos) * invDt;
                // Blend rather than replace — preserve gravity/impulse
                // contributions that PBD didn't undo.
                vel->value = 0.5f * vel->value + 0.5f * newVel;
            }
        }
        if (auto* angVel = registry.try_get<AngularVelocity>(s.entity)) {
            if (auto* ori = registry.try_get<Orientation>(s.entity)) {
                // Quaternion log gives an axis-angle delta we can divide by dt.
                glm::quat dq = ori->value * glm::inverse(s.originalOri);
                if (dq.w < 0.0f)
                    dq = -dq;
                const float w = std::clamp(dq.w, -1.0f, 1.0f);
                const float angle = 2.0f * std::acos(w);
                const float sinHalf = std::sqrt(std::max(0.0f, 1.0f - w * w));
                glm::vec3 axis{0.0f};
                if (sinHalf > 1e-6f)
                    axis = glm::vec3{dq.x, dq.y, dq.z} / sinHalf;
                const glm::vec3 pbdAngVel = axis * (angle * invDt);
                angVel->value = 0.5f * angVel->value + 0.5f * pbdAngVel;
            }
        }
    }
}

} // namespace physics
