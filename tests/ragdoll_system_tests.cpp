#include "ecs/components/ClientId.hpp"
#include "ecs/components/Orientation.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Ragdoll.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Forces.hpp"
#include "ecs/physics/Joints.hpp"
#include "ecs/physics/RagdollPbd.hpp"
#include "ecs/physics/Solver.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/RagdollSystem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
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
    // Ragdoll now uses PBD joints exclusively — the old PGS joint types are
    // not emitted by spawnRagdoll any more.
    ok &= expect(registry.view<physics::RagdollPbdJoint>().size() == 14u,
                 "spawnRagdoll creates 14 PBD joints (5 point + 4 hinge + 5 cone-twist)");
    ok &= expect(registry.view<physics::PointJoint>().size() == 0u, "ragdoll does not emit legacy PointJoint");
    ok &= expect(registry.view<physics::HingeJoint>().size() == 0u, "ragdoll does not emit legacy HingeJoint");
    ok &= expect(registry.view<physics::ConeTwistJoint>().size() == 0u, "ragdoll does not emit legacy ConeTwistJoint");

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
    ok &= expect(registry.view<physics::RagdollPbdJoint>().size() == 0u, "destroyRagdoll removes PBD joints");
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

    for (auto&& [_, j] : registry.view<physics::RagdollPbdJoint>().each())
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

/// @brief After `spawnRagdoll`, NO pair of adjacent bones (connected by a
/// joint) should have a mass ratio greater than ~3:1. PGS+Baumgarte
/// convergence degrades sharply when adjacent masses differ by more than
/// ~10:1 (Unity/Havok docs); 3:1 is the conservative target for ragdolls
/// at single-digit iteration counts.
bool testJointedMassRatiosBounded()
{
    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<ClientId>(player, ClientId{.value = 11});
    registry.emplace<Position>(player, Position{.value = {0.0f, 0.0f, 0.0f}});
    registry.emplace<Velocity>(player, Velocity{.value = {0.0f, 0.0f, 0.0f}});
    systems::spawnRagdoll(registry, player);

    bool ok = true;
    constexpr float k_maxRatio = 3.0f;

    auto checkPair = [&](entt::entity a, entt::entity b) {
        const auto& ra = registry.get<RigidBody>(a);
        const auto& rb = registry.get<RigidBody>(b);
        const float massA = 1.0f / ra.invMass;
        const float massB = 1.0f / rb.invMass;
        const float ratio = (massA > massB) ? massA / massB : massB / massA;
        if (ratio > k_maxRatio) {
            std::cerr << "FAILED: jointed pair mass ratio " << ratio << " exceeds " << k_maxRatio << '\n';
            ok = false;
        }
    };
    for (auto&& [_, j] : registry.view<physics::RagdollPbdJoint>().each())
        checkPair(j.bodyA, j.bodyB);

    systems::destroyRagdoll(registry, player);
    return ok;
}

/// @brief Drive PBD connectivity for 120 fixed ticks with hostile initial
/// conditions (large angular velocity kicks on multiple bones) and assert
/// the bones NEVER detach from the skeleton. This is the core promise of
/// the rebuild: positions are projected to the rest pose every tick, so
/// no amount of forcing can stretch the joints visibly.
bool testRagdollNeverDetachesUnderKick()
{
    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<ClientId>(player, ClientId{.value = 22});
    const glm::vec3 spawnCenter{0.0f, 100.0f, 0.0f};
    registry.emplace<Position>(player, Position{.value = spawnCenter});
    registry.emplace<Velocity>(player, Velocity{.value = {0.0f, 0.0f, 0.0f}});
    systems::spawnRagdoll(registry, player);
    const auto& ragdoll = registry.get<Ragdoll>(player);

    // Slam multiple bones with strong angular kicks AND a divergent linear
    // velocity so every joint type is stressed simultaneously.
    const entt::entity head = ragdoll.bodies[static_cast<size_t>(RagdollBone::Head)];
    const entt::entity handR = ragdoll.bodies[static_cast<size_t>(RagdollBone::HandR)];
    const entt::entity footL = ragdoll.bodies[static_cast<size_t>(RagdollBone::FootL)];
    registry.get<AngularVelocity>(head).value = glm::vec3{25.0f, 0.0f, 15.0f};
    registry.get<AngularVelocity>(handR).value = glm::vec3{0.0f, 40.0f, 20.0f};
    registry.get<AngularVelocity>(footL).value = glm::vec3{30.0f, 30.0f, 0.0f};
    registry.get<Velocity>(handR).value = glm::vec3{200.0f, 0.0f, 0.0f};
    registry.get<Velocity>(footL).value = glm::vec3{0.0f, -200.0f, 0.0f};

    constexpr float k_dt = 1.0f / 60.0f;
    constexpr int k_ticks = 120;

    auto worldAnchor = [&](entt::entity e, glm::vec3 local) -> glm::vec3 {
        const glm::vec3 pos = registry.get<Position>(e).value;
        const glm::quat ori = registry.get<Orientation>(e).value;
        return pos + ori * local;
    };

    bool ok = true;
    float maxAnchorErr = 0.0f;

    for (int tick = 0; tick < k_ticks; ++tick) {
        // Order mirrors runDynamics: integrate orientation/damping first,
        // then the PBD pass projects positions and clamps angular limits.
        physics::forces::integrateAccumulators(registry, k_dt);
        physics::enforceRagdollConnectivity(registry, k_dt);
        physics::clampVelocities(registry);

        for (auto&& [_, j] : registry.view<physics::RagdollPbdJoint>().each()) {
            const float err = glm::length(worldAnchor(j.bodyA, j.localAnchorA) - worldAnchor(j.bodyB, j.localAnchorB));
            maxAnchorErr = std::max(maxAnchorErr, err);
        }
    }

    // PBD is a hard constraint — anchor error stays at sub-pixel levels.
    // We allow a small tolerance for floating-point round-off across 120
    // ticks of integration + projection. A failure here means PBD didn't
    // converge — i.e. the ragdoll is visibly stretched.
    constexpr float k_maxAnchorTolerance = 0.5f;
    if (maxAnchorErr > k_maxAnchorTolerance) {
        std::cerr << "FAILED: max joint anchor error " << maxAnchorErr << " > " << k_maxAnchorTolerance
                  << " — bones detached under stress test\n";
        ok = false;
    }

    systems::destroyRagdoll(registry, player);
    return ok;
}

/// @brief Verify the angular-limit clamp actually holds the relative
/// rotation within each joint's swing/twist envelope. Drive the head
/// with a large angular velocity around an axis far from its hinge, run
/// PBD for 60 ticks, then assert the head-to-torso relative rotation
/// stays within the cone-twist limit (Neck: swing 0.7, twist 0.6).
bool testAngularLimitsHold()
{
    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<ClientId>(player, ClientId{.value = 33});
    registry.emplace<Position>(player, Position{.value = {0.0f, 0.0f, 0.0f}});
    registry.emplace<Velocity>(player, Velocity{.value = {0.0f, 0.0f, 0.0f}});
    systems::spawnRagdoll(registry, player);
    const auto& ragdoll = registry.get<Ragdoll>(player);

    const entt::entity head = ragdoll.bodies[static_cast<size_t>(RagdollBone::Head)];
    const entt::entity torso = ragdoll.bodies[static_cast<size_t>(RagdollBone::Torso)];
    registry.get<AngularVelocity>(head).value = glm::vec3{50.0f, 50.0f, 50.0f};

    constexpr float k_dt = 1.0f / 60.0f;
    for (int tick = 0; tick < 60; ++tick) {
        physics::forces::integrateAccumulators(registry, k_dt);
        physics::enforceRagdollConnectivity(registry, k_dt);
        physics::clampVelocities(registry);
    }

    // Find the neck joint (cone-twist, swing 0.7 + twist 0.6 → max
    // relative angle ~swing + twist ≈ 1.3 rad).
    constexpr float k_maxRelativeAngle = 1.4f; // small slack for swing+twist composition.

    const glm::quat qHead = registry.get<Orientation>(head).value;
    const glm::quat qTorso = registry.get<Orientation>(torso).value;
    glm::quat qRel = glm::inverse(qTorso) * qHead;
    if (qRel.w < 0.0f)
        qRel = -qRel;
    const float w = std::clamp(qRel.w, -1.0f, 1.0f);
    const float relAngle = 2.0f * std::acos(w);

    if (relAngle > k_maxRelativeAngle) {
        std::cerr << "FAILED: head-torso relative angle " << relAngle << " rad exceeds limit " << k_maxRelativeAngle
                  << '\n';
        systems::destroyRagdoll(registry, player);
        return false;
    }

    systems::destroyRagdoll(registry, player);
    return true;
}

int main()
{
    if (!testRagdollSpawnAndDestroy())
        return EXIT_FAILURE;
    if (!testJointAnchorsCoincide())
        return EXIT_FAILURE;
    if (!testBonesNearSpawnCenter())
        return EXIT_FAILURE;
    if (!testJointedMassRatiosBounded())
        return EXIT_FAILURE;
    if (!testRagdollNeverDetachesUnderKick())
        return EXIT_FAILURE;
    if (!testAngularLimitsHold())
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
