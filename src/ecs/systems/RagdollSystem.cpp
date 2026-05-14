/// @file RagdollSystem.cpp
/// @brief Implementation of ragdoll spawn / tick.

#include "ecs/systems/RagdollSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
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
    float halfHeight; ///< Cylinder portion; total bone height = 2*(halfHeight+radius).
    float mass;
};

/// @brief 15-body humanoid layout (~80 kg total).  Values are tuned to a
/// 72-unit-tall character with mass distribution roughly matching a
/// human (head ~5 %, torso ~40 %, arms ~10 %, legs ~30 %).
constexpr BoneDesc k_bones[] = {
    {RagdollBone::Head,      {0, 32, 0}, 7, 3, 4},
    {RagdollBone::Torso,     {0, 18, 0}, 11, 10, 30},
    {RagdollBone::Pelvis,    {0, 0, 0}, 11, 4, 8},
    {RagdollBone::UpperArmL, {-14, 22, 0}, 4, 8, 4},
    {RagdollBone::UpperArmR, {14, 22, 0}, 4, 8, 4},
    {RagdollBone::ForearmL,  {-22, 12, 0}, 3, 7, 2.5f},
    {RagdollBone::ForearmR,  {22, 12, 0}, 3, 7, 2.5f},
    {RagdollBone::HandL,     {-26, 4, 0}, 3, 2, 1},
    {RagdollBone::HandR,     {26, 4, 0}, 3, 2, 1},
    {RagdollBone::UpperLegL, {-6, -8, 0}, 5, 10, 9},
    {RagdollBone::UpperLegR, {6, -8, 0}, 5, 10, 9},
    {RagdollBone::LowerLegL, {-6, -22, 0}, 4, 8, 5},
    {RagdollBone::LowerLegR, {6, -22, 0}, 4, 8, 5},
    {RagdollBone::FootL,     {-6, -34, 4}, 4, 2, 1},
    {RagdollBone::FootR,     {6, -34, 4}, 4, 2, 1},
};

/// @brief One joint in the ragdoll skeleton.  We use point joints
/// universally — cleanest visual behaviour without conetwist tuning.
/// Hinges could replace specific entries (elbows / knees) for tighter
/// behaviour but require additional axis tuning.
struct JointDesc
{
    RagdollBone parent;
    RagdollBone child;
    glm::vec3 anchorLocalToParent;
    glm::vec3 anchorLocalToChild;
};

constexpr JointDesc k_joints[] = {
    {RagdollBone::Torso, RagdollBone::Head, {0, 14, 0}, {0, -10, 0}},      ///< Neck
    {RagdollBone::Torso, RagdollBone::Pelvis, {0, -14, 0}, {0, 6, 0}},     ///< Spine
    {RagdollBone::Torso, RagdollBone::UpperArmL, {-12, 6, 0}, {2, 8, 0}},  ///< Shoulder L
    {RagdollBone::Torso, RagdollBone::UpperArmR, {12, 6, 0}, {-2, 8, 0}},  ///< Shoulder R
    {RagdollBone::UpperArmL, RagdollBone::ForearmL, {0, -10, 0}, {0, 9, 0}}, ///< Elbow L
    {RagdollBone::UpperArmR, RagdollBone::ForearmR, {0, -10, 0}, {0, 9, 0}}, ///< Elbow R
    {RagdollBone::ForearmL, RagdollBone::HandL, {0, -9, 0}, {0, 4, 0}},     ///< Wrist L
    {RagdollBone::ForearmR, RagdollBone::HandR, {0, -9, 0}, {0, 4, 0}},     ///< Wrist R
    {RagdollBone::Pelvis, RagdollBone::UpperLegL, {-6, -4, 0}, {0, 12, 0}}, ///< Hip L
    {RagdollBone::Pelvis, RagdollBone::UpperLegR, {6, -4, 0}, {0, 12, 0}},  ///< Hip R
    {RagdollBone::UpperLegL, RagdollBone::LowerLegL, {0, -12, 0}, {0, 10, 0}}, ///< Knee L
    {RagdollBone::UpperLegR, RagdollBone::LowerLegR, {0, -12, 0}, {0, 10, 0}}, ///< Knee R
    {RagdollBone::LowerLegL, RagdollBone::FootL, {0, -10, 0}, {0, 2, 0}},    ///< Ankle L
    {RagdollBone::LowerLegR, RagdollBone::FootR, {0, -10, 0}, {0, 2, 0}},    ///< Ankle R
};
static_assert(std::size(k_joints) == 14u, "14 joints expected for 15-body tree");

entt::entity createBone(Registry& registry, entt::entity character, const BoneDesc& bd, glm::vec3 charCenter,
                       glm::vec3 charLinearVel)
{
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

    registry.emplace<RagdollBoneTag>(body, RagdollBoneTag{.character = character, .bone = bd.bone});

    return body;
}

entt::entity createJoint(Registry& registry, entt::entity bodyA, entt::entity bodyB, glm::vec3 anchorA,
                       glm::vec3 anchorB)
{
    entt::entity j = registry.create();
    physics::PointJoint pj{};
    pj.bodyA = bodyA;
    pj.bodyB = bodyB;
    pj.localAnchorA = anchorA;
    pj.localAnchorB = anchorB;
    registry.emplace<physics::PointJoint>(j, pj);
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
        rag.joints[jointIdx++] = createJoint(registry, parent, child, jd.anchorLocalToParent, jd.anchorLocalToChild);
    }

    registry.emplace<Ragdoll>(character, rag);
    return character;
}

void runRagdolls(Registry& registry, float dt)
{
    auto view = registry.view<Ragdoll>();
    for (auto e : view) {
        view.get<Ragdoll>(e).age += dt;
    }
}

} // namespace systems
