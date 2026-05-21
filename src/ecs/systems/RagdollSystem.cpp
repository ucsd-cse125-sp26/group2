/// @file RagdollSystem.cpp
/// @brief Implementation of ragdoll spawn / tick.

#include "ecs/systems/RagdollSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Ragdoll.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Inertia.hpp"
#include "ecs/physics/Joints.hpp"

#include <glm/vec3.hpp>

namespace systems
{

namespace
{

/// @brief Single bone entry in the humanoid ragdoll layout.  Positions are
/// relative to the character's centre (the source `Position::value`),
/// using the existing 72-unit standing-player height as reference:
///   - feet at y = 0
///   - centre at y ≈ 36
///   - head top at y ≈ 72
struct BoneDesc
{
    RagdollBone bone;
    glm::vec3 centerOffset; ///< Bone centre offset from character centre.
    float radius;
    float halfHeight;       ///< Cylinder portion; total bone height = 2*(halfHeight+radius).
    float mass;
};

/// @brief 15-body humanoid layout (~80 kg total).  Values are tuned to a
/// 72-unit-tall character with mass distribution roughly matching a
/// human (head ~5 %, torso ~40 %, arms ~10 %, legs ~30 %).
constexpr BoneDesc k_bones[] = {
    {RagdollBone::Head, {0, 32, 0}, 7, 3, 4},
    {RagdollBone::Torso, {0, 18, 0}, 11, 10, 30},
    {RagdollBone::Pelvis, {0, 0, 0}, 11, 4, 8},
    {RagdollBone::UpperArmL, {-14, 22, 0}, 4, 8, 4},
    {RagdollBone::UpperArmR, {14, 22, 0}, 4, 8, 4},
    {RagdollBone::ForearmL, {-22, 12, 0}, 3, 7, 2.5f},
    {RagdollBone::ForearmR, {22, 12, 0}, 3, 7, 2.5f},
    {RagdollBone::HandL, {-26, 4, 0}, 3, 2, 1},
    {RagdollBone::HandR, {26, 4, 0}, 3, 2, 1},
    {RagdollBone::UpperLegL, {-6, -8, 0}, 5, 10, 9},
    {RagdollBone::UpperLegR, {6, -8, 0}, 5, 10, 9},
    {RagdollBone::LowerLegL, {-6, -22, 0}, 4, 8, 5},
    {RagdollBone::LowerLegR, {6, -22, 0}, 4, 8, 5},
    {RagdollBone::FootL, {-6, -34, 4}, 4, 2, 1},
    {RagdollBone::FootR, {6, -34, 4}, 4, 2, 1},
};

/// @brief One joint in the ragdoll skeleton.  Phase 13 follow-up: each
/// joint declares its kind so the spawn code can build the appropriate
/// constraint — cone-twist for shoulders/hips/neck (smooth ball-socket
/// range with twist limits), hinge for elbows/knees (1-DOF rotation),
/// point for wrists/ankles/spine (no extra constraint, just locked
/// anchor).
struct JointDesc
{
    enum class Kind : uint8_t
    {
        Point,    ///< Anchor-only (full freedom).
        Hinge,    ///< 1-DOF rotation about `axisInParent`.
        ConeTwist ///< Cone swing + twist limits about `axisInParent`.
    };

    RagdollBone parent;
    RagdollBone child;
    glm::vec3 anchorLocalToParent;
    glm::vec3 anchorLocalToChild;
    Kind kind = Kind::Point;
    glm::vec3 axisInParent{0, 0, 1};
    float swingLimit = 0.6f; ///< Cone-twist only — radians.
    float twistLimit = 0.5f; ///< Cone-twist only.
    float hingeMin = -1.5f;  ///< Hinge only.
    float hingeMax = 0.05f;  ///< Hinge only.
};

constexpr JointDesc k_joints[] = {
    {RagdollBone::Torso,
     RagdollBone::Head,
     {0, 14, 0},
     {0, -10, 0},
     JointDesc::Kind::ConeTwist,
     {1, 0, 0},
     0.7f,
     0.6f,
     0,
     0}, ///< Neck — moderate cone, twist
    {RagdollBone::Torso,
     RagdollBone::Pelvis,
     {0, -14, 0},
     {0, 6, 0},
     JointDesc::Kind::ConeTwist,
     {1, 0, 0},
     0.4f,
     0.3f,
     0,
     0}, ///< Spine — small
    {RagdollBone::Torso,
     RagdollBone::UpperArmL,
     {-12, 6, 0},
     {2, 8, 0},
     JointDesc::Kind::ConeTwist,
     {0, -1, 0},
     1.4f,
     1.0f,
     0,
     0}, ///< Shoulder L — wide cone
    {RagdollBone::Torso,
     RagdollBone::UpperArmR,
     {12, 6, 0},
     {-2, 8, 0},
     JointDesc::Kind::ConeTwist,
     {0, -1, 0},
     1.4f,
     1.0f,
     0,
     0}, ///< Shoulder R
    {RagdollBone::UpperArmL,
     RagdollBone::ForearmL,
     {0, -10, 0},
     {0, 9, 0},
     JointDesc::Kind::Hinge,
     {1, 0, 0},
     0,
     0,
     -2.0f,
     0.05f}, ///< Elbow L (flex only)
    {RagdollBone::UpperArmR,
     RagdollBone::ForearmR,
     {0, -10, 0},
     {0, 9, 0},
     JointDesc::Kind::Hinge,
     {1, 0, 0},
     0,
     0,
     -2.0f,
     0.05f}, ///< Elbow R
    {RagdollBone::ForearmL,
     RagdollBone::HandL,
     {0, -9, 0},
     {0, 4, 0},
     JointDesc::Kind::Point,
     {0, 0, 1},
     0,
     0,
     0,
     0}, ///< Wrist L
    {RagdollBone::ForearmR,
     RagdollBone::HandR,
     {0, -9, 0},
     {0, 4, 0},
     JointDesc::Kind::Point,
     {0, 0, 1},
     0,
     0,
     0,
     0}, ///< Wrist R
    {RagdollBone::Pelvis,
     RagdollBone::UpperLegL,
     {-6, -4, 0},
     {0, 12, 0},
     JointDesc::Kind::ConeTwist,
     {0, -1, 0},
     1.0f,
     0.4f,
     0,
     0}, ///< Hip L
    {RagdollBone::Pelvis,
     RagdollBone::UpperLegR,
     {6, -4, 0},
     {0, 12, 0},
     JointDesc::Kind::ConeTwist,
     {0, -1, 0},
     1.0f,
     0.4f,
     0,
     0}, ///< Hip R
    {RagdollBone::UpperLegL,
     RagdollBone::LowerLegL,
     {0, -12, 0},
     {0, 10, 0},
     JointDesc::Kind::Hinge,
     {1, 0, 0},
     0,
     0,
     0.0f,
     2.2f}, ///< Knee L (flex back only)
    {RagdollBone::UpperLegR,
     RagdollBone::LowerLegR,
     {0, -12, 0},
     {0, 10, 0},
     JointDesc::Kind::Hinge,
     {1, 0, 0},
     0,
     0,
     0.0f,
     2.2f}, ///< Knee R
    {RagdollBone::LowerLegL,
     RagdollBone::FootL,
     {0, -10, 0},
     {0, 2, 0},
     JointDesc::Kind::Point,
     {0, 0, 1},
     0,
     0,
     0,
     0}, ///< Ankle L
    {RagdollBone::LowerLegR,
     RagdollBone::FootR,
     {0, -10, 0},
     {0, 2, 0},
     JointDesc::Kind::Point,
     {0, 0, 1},
     0,
     0,
     0,
     0}, ///< Ankle R
};
static_assert(std::size(k_joints) == 14u, "14 joints expected for 15-body tree");

entt::entity createBone(
    Registry& registry, entt::entity character, const BoneDesc& bd, glm::vec3 charCenter, glm::vec3 charLinearVel)
{
    const ClientId characterId = registry.all_of<ClientId>(character) ? registry.get<ClientId>(character) : ClientId{};
    entt::entity body = registry.create();
    registry.emplace<Position>(body, Position{.value = charCenter + bd.centerOffset});
    registry.emplace<Velocity>(body, Velocity{.value = charLinearVel});
    registry.emplace<Orientation>(body);
    registry.emplace<AngularVelocity>(body);
    registry.emplace<CollisionShape>(body,
                                     CollisionShape{
                                         .type = CollisionShapeType::Capsule,
                                         .halfExtents = {bd.radius, bd.halfHeight + bd.radius, bd.radius},
                                         .radius = bd.radius,
                                         .halfHeight = bd.halfHeight,
                                     });

    RigidBody rb;
    rb.invMass = 1.0f / bd.mass;
    rb.localInvInertia = physics::inertia::capsuleInvInertia(bd.mass, bd.radius, bd.halfHeight);
    rb.invInertiaWorld = rb.localInvInertia; // identity orientation at spawn
    rb.linearDamping = 0.1f;
    rb.angularDamping = 0.3f;
    registry.emplace<RigidBody>(body, rb);

    registry.emplace<RagdollBoneTag>(body,
                                     RagdollBoneTag{.character = character, .characterId = characterId, .bone = bd.bone});

    return body;
}

void destroyIfOwnedChild(Registry& registry, entt::entity owner, entt::entity child)
{
    if (child == entt::null || child == owner || !registry.valid(child))
        return;

    registry.destroy(child);
}

entt::entity createJoint(Registry& registry, entt::entity bodyA, entt::entity bodyB, const JointDesc& jd)
{
    entt::entity j = registry.create();
    switch (jd.kind) {
    case JointDesc::Kind::Point: {
        physics::PointJoint pj{};
        pj.bodyA = bodyA;
        pj.bodyB = bodyB;
        pj.localAnchorA = jd.anchorLocalToParent;
        pj.localAnchorB = jd.anchorLocalToChild;
        registry.emplace<physics::PointJoint>(j, pj);
        break;
    }
    case JointDesc::Kind::Hinge: {
        physics::HingeJoint hj{};
        hj.bodyA = bodyA;
        hj.bodyB = bodyB;
        hj.localAnchorA = jd.anchorLocalToParent;
        hj.localAnchorB = jd.anchorLocalToChild;
        hj.localAxisA = jd.axisInParent;
        hj.localAxisB = jd.axisInParent;
        hj.hasLimit = true;
        hj.minAngle = jd.hingeMin;
        hj.maxAngle = jd.hingeMax;
        registry.emplace<physics::HingeJoint>(j, hj);
        break;
    }
    case JointDesc::Kind::ConeTwist: {
        physics::ConeTwistJoint cj{};
        cj.bodyA = bodyA;
        cj.bodyB = bodyB;
        cj.localAnchorA = jd.anchorLocalToParent;
        cj.localAnchorB = jd.anchorLocalToChild;
        cj.swingLimit = jd.swingLimit;
        cj.twistLimit = jd.twistLimit;
        // Orient the local joint frame so +X aligns with the desired
        // twist axis.  Default is identity (axis = +X local).  When a
        // different axis is requested we build the rotation that takes
        // +X to that axis.
        const glm::vec3 to = glm::normalize(jd.axisInParent);
        const glm::vec3 from{1, 0, 0};
        const float d = glm::dot(from, to);
        if (d > 0.9999f) {
            cj.localFrameA = glm::quat{1, 0, 0, 0};
        } else if (d < -0.9999f) {
            cj.localFrameA = glm::quat{0, 0, 1, 0}; // 180° about Y
        } else {
            const glm::vec3 axis = glm::normalize(glm::cross(from, to));
            const float angle = std::acos(d);
            cj.localFrameA = glm::angleAxis(angle, axis);
        }
        cj.localFrameB = cj.localFrameA;
        registry.emplace<physics::ConeTwistJoint>(j, cj);
        break;
    }
    }
    return j;
}

} // namespace

entt::entity spawnRagdoll(Registry& registry, entt::entity character)
{
    if (registry.try_get<Ragdoll>(character) != nullptr)
        return character; // idempotent

    const auto* charPos = registry.try_get<Position>(character);
    const auto* charVel = registry.try_get<Velocity>(character);
    if (charPos == nullptr)
        return entt::null;

    const glm::vec3 center = charPos->value;
    const glm::vec3 linVel = (charVel != nullptr) ? charVel->value : glm::vec3{0.0f};

    Ragdoll rag;

    // Create bodies in canonical bone order so indices match `bodies[idx]`.
    for (const BoneDesc& bd : k_bones) {
        const auto idx = static_cast<size_t>(bd.bone);
        rag.bodies[idx] = createBone(registry, character, bd, center, linVel);
    }

    // Create joints between bodies.
    size_t jointIdx = 0;
    for (const JointDesc& jd : k_joints) {
        const entt::entity parent = rag.bodies[static_cast<size_t>(jd.parent)];
        const entt::entity child = rag.bodies[static_cast<size_t>(jd.child)];
        rag.joints[jointIdx++] = createJoint(registry, parent, child, jd);
    }

    registry.emplace<Ragdoll>(character, rag);
    return character;
}

void destroyRagdoll(Registry& registry, entt::entity character)
{
    Ragdoll* ragdoll = registry.try_get<Ragdoll>(character);
    if (ragdoll == nullptr)
        return;

    for (const entt::entity joint : ragdoll->joints)
        destroyIfOwnedChild(registry, character, joint);
    for (const entt::entity body : ragdoll->bodies)
        destroyIfOwnedChild(registry, character, body);

    registry.remove<Ragdoll>(character);
}

void runRagdolls(Registry& registry, float dt)
{
    auto view = registry.view<Ragdoll>();
    for (auto e : view) {
        view.get<Ragdoll>(e).age += dt;
    }
}

} // namespace systems
