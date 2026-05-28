#include "ecs/components/ClientId.hpp"
#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Ragdoll.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Joints.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/RagdollSystem.hpp"

#include <glm/glm.hpp>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{

bool expect(bool condition, std::string_view message)
{
    if (condition)
        return true;

    std::cerr << "FAILED: " << message << '\n';
    return false;
}

bool testRagdollSpawnAndDestroy()
{
    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<ClientId>(player, ClientId{.value = 42});
    registry.emplace<Position>(player, Position{.value = {100.0f, 60.0f, -20.0f}});
    registry.emplace<Velocity>(player, Velocity{.value = {1.0f, 2.0f, 3.0f}});

    bool ok = true;
    ok &= expect(systems::spawnRagdoll(registry, player) == player, "spawnRagdoll returns the owning player");
    ok &= expect(registry.all_of<Ragdoll>(player), "spawnRagdoll attaches Ragdoll to the player");
    ok &= expect(registry.view<RagdollBoneTag>().size() == 15u, "spawnRagdoll creates 15 tagged bone entities");
    ok &= expect(registry.view<physics::PointJoint>().size() == 4u, "spawnRagdoll creates wrist/ankle point joints");
    ok &= expect(registry.view<physics::HingeJoint>().size() == 4u, "spawnRagdoll creates elbow/knee hinge joints");
    ok &= expect(registry.view<physics::ConeTwistJoint>().size() == 6u,
                 "spawnRagdoll creates neck/spine/shoulder/hip cone-twist joints");

    const auto& ragdoll = registry.get<Ragdoll>(player);
    for (const entt::entity body : ragdoll.bodies) {
        ok &= expect(registry.valid(body), "ragdoll body handle is valid");
        ok &= expect(registry.all_of<Position, Velocity, RigidBody, RagdollBoneTag>(body),
                     "ragdoll body has physics and tag components");
        const auto& tag = registry.get<RagdollBoneTag>(body);
        ok &= expect(tag.character == player, "ragdoll bone points at its owning player");
        ok &= expect(tag.characterId.value == 42, "ragdoll bone carries replicated owner ClientId");
    }

    systems::destroyRagdoll(registry, player);
    ok &= expect(!registry.all_of<Ragdoll>(player), "destroyRagdoll removes the parent component");
    ok &= expect(registry.view<RagdollBoneTag>().size() == 0u, "destroyRagdoll removes bone entities");
    ok &= expect(registry.view<physics::PointJoint>().size() == 0u, "destroyRagdoll removes point joints");
    ok &= expect(registry.view<physics::HingeJoint>().size() == 0u, "destroyRagdoll removes hinge joints");
    ok &= expect(registry.view<physics::ConeTwistJoint>().size() == 0u, "destroyRagdoll removes cone-twist joints");
    ok &= expect(registry.valid(player), "destroyRagdoll keeps the owning player alive");

    systems::destroyRagdoll(registry, player);
    ok &= expect(registry.valid(player), "destroyRagdoll is safe when no ragdoll exists");

    ok &= expect(systems::spawnRagdoll(registry, player) == player, "ragdoll can be spawned again after destroy");
    ok &= expect(registry.view<RagdollBoneTag>().size() == 15u, "second spawn creates a fresh bone set");
    systems::destroyRagdoll(registry, player);

    return ok;
}

} // namespace

/// @brief Compute the world-space position of a joint anchor on body `e`.
/// At identity orientation (which is the spawn pose) this is just
/// `position + localAnchor`. We still rotate by the orientation quaternion
/// for robustness in case the test is ever extended to non-identity poses.
glm::vec3 worldAnchor(Registry& registry, entt::entity e, glm::vec3 localAnchor)
{
    const glm::vec3 pos = registry.get<Position>(e).value;
    if (const auto* o = registry.try_get<Orientation>(e))
        return pos + o->value * localAnchor;
    return pos + localAnchor;
}

/// @brief After `spawnRagdoll`, every joint's parent and child anchors must
/// coincide in world space (zero initial constraint error). When this invariant
/// is violated the Baumgarte-stabilised solver applies massive corrective
/// impulses each tick, causing the ragdoll to jitter and explode outward.
bool testJointAnchorsCoincide()
{
    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<ClientId>(player, ClientId{.value = 7});
    registry.emplace<Position>(player, Position{.value = {0.0f, 0.0f, 0.0f}});
    registry.emplace<Velocity>(player, Velocity{.value = {0.0f, 0.0f, 0.0f}});

    if (systems::spawnRagdoll(registry, player) == entt::null) {
        std::cerr << "FAILED: spawnRagdoll returned null\n";
        return false;
    }

    bool ok = true;
    constexpr float k_tolerance = 1e-4f; // sub-pixel — anchors must coincide exactly.
    int checked = 0;

    auto checkJoint = [&](entt::entity bodyA, entt::entity bodyB, glm::vec3 anchorA, glm::vec3 anchorB) {
        const glm::vec3 worldA = worldAnchor(registry, bodyA, anchorA);
        const glm::vec3 worldB = worldAnchor(registry, bodyB, anchorB);
        const float error = glm::length(worldA - worldB);
        if (error > k_tolerance) {
            std::cerr << "FAILED: joint anchor error " << error << " exceeds tolerance " << k_tolerance << '\n';
            ok = false;
        }
        ++checked;
    };

    for (auto&& [_, j] : registry.view<physics::PointJoint>().each())
        checkJoint(j.bodyA, j.bodyB, j.localAnchorA, j.localAnchorB);
    for (auto&& [_, j] : registry.view<physics::HingeJoint>().each())
        checkJoint(j.bodyA, j.bodyB, j.localAnchorA, j.localAnchorB);
    for (auto&& [_, j] : registry.view<physics::ConeTwistJoint>().each())
        checkJoint(j.bodyA, j.bodyB, j.localAnchorA, j.localAnchorB);

    ok &= expect(checked == 14, "all 14 ragdoll joints were checked");

    systems::destroyRagdoll(registry, player);
    return ok;
}

/// @brief After `spawnRagdoll`, all bodies should be near the spawn center —
/// the schematic bone offsets in `k_bones` keep every part inside a ~50-unit
/// AABB. A regression in the spawn logic (wrong scale, double-translation,
/// off-by-N in the table) would push some bodies far away from the player.
bool testBonesNearSpawnCenter()
{
    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<ClientId>(player, ClientId{.value = 9});
    const glm::vec3 spawnCenter{200.0f, 60.0f, -50.0f};
    registry.emplace<Position>(player, Position{.value = spawnCenter});
    registry.emplace<Velocity>(player, Velocity{.value = {0.0f, 0.0f, 0.0f}});

    systems::spawnRagdoll(registry, player);

    bool ok = true;
    constexpr float k_maxDistFromCenter = 50.0f; // largest bone offset is ~34 (foot).
    for (auto&& [_, tag, pos] : registry.view<RagdollBoneTag, Position>().each()) {
        (void)tag;
        const float d = glm::length(pos.value - spawnCenter);
        if (d > k_maxDistFromCenter) {
            std::cerr << "FAILED: bone " << d << " units from spawn center (limit " << k_maxDistFromCenter << ")\n";
            ok = false;
        }
    }
    systems::destroyRagdoll(registry, player);
    return ok;
}

int main()
{
    if (!testRagdollSpawnAndDestroy())
        return EXIT_FAILURE;
    if (!testJointAnchorsCoincide())
        return EXIT_FAILURE;
    if (!testBonesNearSpawnCenter())
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
