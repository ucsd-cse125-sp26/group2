#include "ecs/components/ClientId.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Ragdoll.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Joints.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/RagdollSystem.hpp"

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

int main()
{
    if (!testRagdollSpawnAndDestroy())
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
