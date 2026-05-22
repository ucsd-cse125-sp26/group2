#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerSimState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/TriMeshCollision.hpp"
#include "ecs/physics/WallDetection.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/KinematicCharacterController.hpp"
#include "ecs/systems/MovementSystem.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <entt/entity/entity.hpp>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using physics::CapsuleShape;
using physics::DepenContact;
using physics::GroundProbeResult;
using physics::HitResult;
using physics::SphereHitResult;
using physics::StaticWorldBroadphase;
using physics::WorldAABB;
using physics::WorldTriMesh;

bool expect(bool condition, std::string_view message)
{
    if (condition)
        return true;

    std::cerr << "FAILED: " << message << '\n';
    return false;
}

bool expectNear(float actual, float expected, float epsilon, std::string_view message)
{
    if (std::abs(actual - expected) <= epsilon)
        return true;

    std::cerr << "FAILED: " << message << " (actual=" << actual << ", expected=" << expected << ", epsilon=" << epsilon
              << ")\n";
    return false;
}

bool finiteVec3(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

physics::KccFrameResult
runMovementAndKccTick(Registry& registry, entt::entity player, const physics::WorldGeometry& world, float dt)
{
    systems::runMovement(registry, dt, world);

    auto& pos = registry.get<Position>(player);
    auto& vel = registry.get<Velocity>(player);
    auto& shape = registry.get<CollisionShape>(player);
    auto& vis = registry.get<PlayerVisState>(player);
    auto& sim = registry.get<PlayerSimState>(player);
    const auto& input = registry.get<InputSnapshot>(player);

    const physics::KccFrameResult result = systems::runKinematicCharacterController(
        pos.value, vel.value, shape, vis, dt, world, player, sim.jumpedThisTick, &sim);
    systems::reconcileMovementAfterKcc(pos.value, vel.value, shape, vis, sim, input, world, result, dt);
    return result;
}

WorldTriMesh makeCookedMesh(std::initializer_list<glm::vec3> vertices, std::initializer_list<uint32_t> indices)
{
    WorldTriMesh mesh;
    mesh.vertices.assign(vertices.begin(), vertices.end());
    mesh.indices.assign(indices.begin(), indices.end());
    physics::buildTriMeshBVH(mesh);
    physics::weldTriMesh(mesh);
    return mesh;
}

WorldTriMesh makeThinWall()
{
    // A zero-thickness authored collision quad in the YZ plane. This mirrors
    // the Blender collision-map contract: walls are triangles, not boxes.
    return makeCookedMesh(
        {
            {0.0f, 0.0f, -20.0f},
            {0.0f, 40.0f, -20.0f},
            {0.0f, 40.0f, 20.0f},
            {0.0f, 0.0f, 20.0f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });
}

WorldTriMesh makeThinWallSegment(float z0, float z1)
{
    return makeCookedMesh(
        {
            {0.0f, 0.0f, z0},
            {0.0f, 40.0f, z0},
            {0.0f, 40.0f, z1},
            {0.0f, 0.0f, z1},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });
}

WorldTriMesh makeThinDepthWall()
{
    return makeCookedMesh(
        {
            {-20.0f, 0.0f, 0.0f},
            {20.0f, 0.0f, 0.0f},
            {20.0f, 40.0f, 0.0f},
            {-20.0f, 40.0f, 0.0f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });
}

WorldTriMesh makeThinDepthWallAt(float z)
{
    return makeCookedMesh(
        {
            {-32.0f, 0.0f, z},
            {8.0f, 0.0f, z},
            {8.0f, 40.0f, z},
            {-32.0f, 40.0f, z},
        },
        {
            0,
            2,
            1,
            0,
            3,
            2,
        });
}

WorldTriMesh makeSingleMeshOutsideWallCorner()
{
    // Two thin wall quads authored as one mesh and sharing the vertical seam
    // at x=0,z=20. This is the real adjacency case: no mesh-to-mesh handoff,
    // just a neighbour triangle across a cooked edge.
    return makeCookedMesh(
        {
            {0.0f, 0.0f, -20.0f},
            {0.0f, 40.0f, -20.0f},
            {0.0f, 40.0f, 20.0f},
            {0.0f, 0.0f, 20.0f},
            {-120.0f, 0.0f, 20.0f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
            4,
            2,
            3,
        });
}

std::array<WorldTriMesh, 3> makeAuthoredMapWallrunCorridorSegment()
{
    constexpr float xWall = 687.566f;
    constexpr float zLowerWall = 386.311f;
    constexpr float zUpperWall = 593.671f;
    constexpr float yMin = 12.853f;
    constexpr float yMax = 252.853f;
    constexpr float corridorEndX = 964.046f;

    WorldTriMesh mainWall = makeCookedMesh(
        {
            {xWall, yMin, zUpperWall},
            {xWall, yMin, 2131.591f},
            {xWall, yMax, 2131.591f},
            {xWall, yMax, zUpperWall},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    WorldTriMesh upperCorridorWall = makeCookedMesh(
        {
            {xWall, yMin, zUpperWall},
            {corridorEndX, yMin, zUpperWall},
            {corridorEndX, yMax, zUpperWall},
            {xWall, yMax, zUpperWall},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    WorldTriMesh lowerCorridorWall = makeCookedMesh(
        {
            {corridorEndX, yMin, zLowerWall},
            {xWall, yMin, zLowerWall},
            {xWall, yMax, zLowerWall},
            {corridorEndX, yMax, zLowerWall},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    return {mainWall, upperCorridorWall, lowerCorridorWall};
}

std::array<WorldTriMesh, 2> makeAuthoredMapExternalLCornerSegment()
{
    constexpr float xWall = 687.566f;
    constexpr float zCorner = 593.671f;
    constexpr float yMin = 12.853f;
    constexpr float yMax = 252.853f;
    constexpr float corridorEndX = 964.046f;

    WorldTriMesh approachWall = makeCookedMesh(
        {
            {xWall, yMin, 213.671f},
            {xWall, yMin, zCorner},
            {xWall, yMax, zCorner},
            {xWall, yMax, 213.671f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    WorldTriMesh continuationWall = makeCookedMesh(
        {
            {xWall, yMin, zCorner},
            {corridorEndX, yMin, zCorner},
            {corridorEndX, yMax, zCorner},
            {xWall, yMax, zCorner},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    return {approachWall, continuationWall};
}

std::array<WorldTriMesh, 2> makeCapturedBalconyDiagonalCornerSegment()
{
    // Extracted from the 2026-05-20 18:53 map capture around
    // x ~= 190, z ~= -830: a finite diagonal octagon wall ending beside a
    // thin balcony side face.  The player wallruns along the diagonal and
    // reaches the balcony side around mid-body height.
    WorldTriMesh diagonalWall = makeCookedMesh(
        {
            {429.9f, 12.9f, -1047.4f},
            {188.3f, 12.9f, -805.7f},
            {188.3f, -216.0f, -805.7f},
            {429.9f, -216.0f, -1047.4f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    WorldTriMesh balconySide = makeCookedMesh(
        {
            {174.0f, 12.9f, -805.7f},
            {174.0f, 12.9f, -935.7f},
            {174.0f, -6.4f, -935.7f},
            {174.0f, -29.1f, -805.7f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    return {diagonalWall, balconySide};
}

std::array<WorldTriMesh, 2> makeDisconnectedOctagonFaceTransition()
{
    constexpr float yMin = -260.0f;
    constexpr float yMax = 80.0f;
    constexpr float oldWallX = 429.920f;
    constexpr float oldWallEndZ = -1076.756f;
    constexpr float newWallStartX = 429.920f;
    constexpr float newWallStartZ = -1070.000f;
    constexpr float diagLen = 180.0f;

    WorldTriMesh approachWall = makeCookedMesh(
        {
            {oldWallX, yMin, -1220.0f},
            {oldWallX, yMin, oldWallEndZ},
            {oldWallX, yMax, oldWallEndZ},
            {oldWallX, yMax, -1220.0f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    WorldTriMesh continuationWall = makeCookedMesh(
        {
            {newWallStartX, yMin, newWallStartZ},
            {newWallStartX - diagLen, yMin, newWallStartZ + diagLen},
            {newWallStartX - diagLen, yMax, newWallStartZ + diagLen},
            {newWallStartX, yMax, newWallStartZ},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });

    return {approachWall, continuationWall};
}

std::array<WorldTriMesh, 2> makeCapturedMapOctagonFirstFaceTransition()
{
    // Extracted from assets/maps/map1.glb around the first octagon handoff in
    // server-movement-diag-20260520-211330.csv rows 396-399. These are the
    // actual authored collision planes that meet at x=429.9,z=-1047.4.
    constexpr float yMin = -216.0f;
    constexpr float yMax = 12.9f;

    WorldTriMesh approachWall = makeCookedMesh(
        {
            {429.9f, yMax, -1129.6f},
            {429.9f, yMax, -1047.4f},
            {429.9f, yMin, -1047.4f},
            {429.9f, yMin, -1129.6f},
        },
        {
            0,
            2,
            3,
            0,
            1,
            2,
        });

    WorldTriMesh continuationWall = makeCookedMesh(
        {
            {429.9f, yMax, -1047.4f},
            {188.3f, yMax, -805.7f},
            {188.3f, yMin, -805.7f},
            {429.9f, yMin, -1047.4f},
        },
        {
            0,
            2,
            3,
            0,
            1,
            2,
        });

    return {approachWall, continuationWall};
}

WorldTriMesh makeTwoTriangleFloor()
{
    return makeCookedMesh(
        {
            {-64.0f, 0.0f, -64.0f},
            {64.0f, 0.0f, -64.0f},
            {64.0f, 0.0f, 64.0f},
            {-64.0f, 0.0f, 64.0f},
        },
        {
            0,
            2,
            1,
            0,
            3,
            2,
        });
}

WorldTriMesh makeTwoTriangleFloorWithDuplicatedSeamVertices()
{
    return makeCookedMesh(
        {
            {-64.0f, 0.0f, -64.0f},
            {64.0f, 0.0f, -64.0f},
            {64.0f, 0.0f, 64.0f},
            {-64.0f, 0.0f, -64.0f},
            {64.0f, 0.0f, 64.0f},
            {-64.0f, 0.0f, 64.0f},
        },
        {
            0,
            2,
            1,
            3,
            5,
            4,
        });
}

WorldTriMesh makeThinSingleStep()
{
    return makeCookedMesh(
        {
            {-64.0f, 16.0f, 0.0f},
            {64.0f, 16.0f, 0.0f},
            {64.0f, 16.0f, 48.0f},
            {-64.0f, 16.0f, 48.0f},
            {-64.0f, 0.0f, 0.0f},
            {64.0f, 0.0f, 0.0f},
            {64.0f, 16.0f, 0.0f},
            {-64.0f, 16.0f, 0.0f},
        },
        {
            0,
            2,
            1,
            0,
            3,
            2,
            4,
            6,
            5,
            4,
            7,
            6,
        });
}

WorldTriMesh makeFloorWithDegenerateRaisedEdge()
{
    // Mirrors the captured map failure: an exported zero-area triangle sits
    // inside the grounded snap window near a valid tread/floor.
    return makeCookedMesh(
        {
            {16.0f, 6.912f, 32.0f},
            {16.0f, 6.912f, 32.0f},
            {16.0f, 6.912f, -32.0f},
            {-64.0f, 0.0f, -64.0f},
            {64.0f, 0.0f, -64.0f},
            {64.0f, 0.0f, 64.0f},
            {-64.0f, 0.0f, 64.0f},
        },
        {
            0,
            1,
            2,
            3,
            5,
            4,
            3,
            6,
            5,
        });
}

WorldTriMesh makeThinStaircase(int steps, float stepHeight, float treadDepth)
{
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    auto addQuad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back(a);
        vertices.push_back(b);
        vertices.push_back(c);
        vertices.push_back(d);
        indices.push_back(base + 0u);
        indices.push_back(base + 2u);
        indices.push_back(base + 1u);
        indices.push_back(base + 0u);
        indices.push_back(base + 3u);
        indices.push_back(base + 2u);
    };

    addQuad({-64.0f, 0.0f, -96.0f}, {64.0f, 0.0f, -96.0f}, {64.0f, 0.0f, 0.0f}, {-64.0f, 0.0f, 0.0f});
    for (int i = 0; i < steps; ++i) {
        const float y0 = static_cast<float>(i) * stepHeight;
        const float y1 = static_cast<float>(i + 1) * stepHeight;
        const float z0 = static_cast<float>(i) * treadDepth;
        const float z1 = static_cast<float>(i + 1) * treadDepth;
        addQuad({-64.0f, y1, z0}, {64.0f, y1, z0}, {64.0f, y1, z1}, {-64.0f, y1, z1});
        addQuad({-64.0f, y0, z0}, {64.0f, y0, z0}, {64.0f, y1, z0}, {-64.0f, y1, z0});
    }

    WorldTriMesh mesh;
    mesh.vertices = std::move(vertices);
    mesh.indices = std::move(indices);
    physics::buildTriMeshBVH(mesh);
    physics::weldTriMesh(mesh);
    return mesh;
}

WorldTriMesh makeThinCeiling()
{
    return makeCookedMesh(
        {
            {-64.0f, 44.0f, -64.0f},
            {64.0f, 44.0f, -64.0f},
            {64.0f, 44.0f, 64.0f},
            {-64.0f, 44.0f, 64.0f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });
}

WorldTriMesh makeShortThinCeiling(float z0, float z1)
{
    return makeCookedMesh(
        {
            {-64.0f, 44.0f, z0},
            {64.0f, 44.0f, z0},
            {64.0f, 44.0f, z1},
            {-64.0f, 44.0f, z1},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });
}

WorldTriMesh makeSlopedCeiling()
{
    // A large, non-horizontal ceiling plane above the capsule. The raw
    // winding points downward, matching common underside collision export;
    // ground probing must not flip it into walkable support from below.
    return makeCookedMesh(
        {
            {300.0f, -168.0f, 264.0f},
            {-300.0f, -168.0f, 264.0f},
            {0.0f, 192.0f, -216.0f},
        },
        {
            0,
            1,
            2,
        });
}

WorldTriMesh makeShallowSlopedCeiling()
{
    constexpr float slope = 0.049f;
    auto yAtZ = [](float z) { return 180.0f - slope * z; };

    return makeCookedMesh(
        {
            {-300.0f, yAtZ(-900.0f), -900.0f},
            {300.0f, yAtZ(-900.0f), -900.0f},
            {300.0f, yAtZ(100.0f), 100.0f},
            {-300.0f, yAtZ(100.0f), 100.0f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });
}

WorldTriMesh makeThinRamp()
{
    return makeCookedMesh(
        {
            {-64.0f, 0.0f, 0.0f},
            {64.0f, 0.0f, 0.0f},
            {64.0f, 32.0f, 64.0f},
            {-64.0f, 32.0f, 64.0f},
        },
        {
            0,
            2,
            1,
            0,
            3,
            2,
        });
}

bool thinWallPlaneBlocksFromBothSides()
{
    const WorldTriMesh wall = makeThinWall();
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 4.0f, .up = {0.0f, 1.0f, 0.0f}};

    const HitResult leftToRight =
        physics::sweepCapsuleVsTriMesh(capsule, {-24.0f, 20.0f, 0.0f}, {24.0f, 20.0f, 0.0f}, wall);
    const HitResult rightToLeft =
        physics::sweepCapsuleVsTriMesh(capsule, {24.0f, 20.0f, 0.0f}, {-24.0f, 20.0f, 0.0f}, wall);

    bool ok = true;
    ok &= expect(leftToRight.hit, "thin wall plane should block capsule motion from the negative side");
    ok &= expect(leftToRight.normal.x < -0.99f,
                 "left-to-right wall hit normal should point back toward the negative side");
    ok &= expect(rightToLeft.hit, "thin wall plane should block capsule motion from the positive side");
    ok &= expect(rightToLeft.normal.x > 0.99f,
                 "right-to-left wall hit normal should point back toward the positive side");
    return ok;
}

bool simultaneousCornerSweepTieBreakIsMeshOrderIndependent()
{
    std::array<WorldTriMesh, 2> xThenZ{makeThinWall(), makeThinDepthWall()};
    std::array<WorldTriMesh, 2> zThenX{xThenZ[1], xThenZ[0]};
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 4.0f, .up = {0.0f, 1.0f, 0.0f}};

    const physics::WorldGeometry worldXThenZ{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(xThenZ),
    };
    const physics::WorldGeometry worldZThenX{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(zThenX),
    };

    const glm::vec3 start{-24.0f, 20.0f, -24.0f};
    const glm::vec3 end{24.0f, 20.0f, 24.0f};
    const HitResult first = physics::sweepAll(capsule, start, end, worldXThenZ);
    const HitResult second = physics::sweepAll(capsule, start, end, worldZThenX);

    bool ok = true;
    ok &= expect(first.hit && second.hit, "simultaneous corner sweep should hit both mesh-order variants");
    ok &= expectNear(first.tFirst, second.tFirst, 0.0001f, "simultaneous corner sweep should keep the same TOI");
    ok &= expectNear(first.normal.x, second.normal.x, 0.001f, "corner tie-break should not depend on mesh order");
    ok &= expectNear(first.normal.z, second.normal.z, 0.001f, "corner tie-break should not depend on mesh order");
    ok &= expect(first.normal.x < -0.99f, "corner tie-break should prefer the canonical X wall normal");
    return ok;
}

bool walkCapsuleStepsOntoThinTrimeshTread()
{
    const WorldTriMesh step = makeThinSingleStep();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&step, 1),
    };

    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 20.0f, .up = {0.0f, 1.0f, 0.0f}};
    const CapsuleShape walkCapsule = capsule.walkShape(18.0f);
    const glm::vec3 walkOffset = capsule.walkCenterOffset(18.0f);

    const glm::vec3 start{0.0f, 30.0f, -24.0f};
    const glm::vec3 end{0.0f, 30.0f, 24.0f};

    const HitResult horizontal = physics::sweepAll(walkCapsule, start + walkOffset, end + walkOffset, world);
    const GroundProbeResult ground = physics::probeGround(capsule, end, 26.0f, world);

    bool ok = true;
    ok &= expect(!horizontal.hit, "walk capsule should ignore a thin riser within step height");
    ok &= expect(ground.hit, "ground probe should recover an overlapping authored tread after horizontal step motion");
    ok &= expect(ground.walkable, "step tread should classify as walkable");
    ok &= expect(ground.distance < 0.0f, "ground probe should report overlapping tread as negative distance");
    ok &= expectNear(ground.point.y, 16.0f, 0.05f, "ground probe should report the authored tread surface");
    ok &= expect(ground.normal.y > 0.99f, "ground probe should keep the tread normal stable");
    return ok;
}

bool groundProbeIgnoresDegenerateSnapCandidate()
{
    const WorldTriMesh floor = makeFloorWithDegenerateRaisedEdge();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&floor, 1),
    };

    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 20.0f, .up = {0.0f, 1.0f, 0.0f}};
    const GroundProbeResult ground = physics::probeGround(capsule, {0.0f, 30.03125f, 0.0f}, 26.0f, world);

    bool ok = true;
    ok &= expect(ground.hit, "ground probe should still find the valid floor below a degenerate exported triangle");
    ok &= expect(finiteVec3(ground.point), "ground probe point should remain finite around degenerate triangles");
    ok &= expect(finiteVec3(ground.normal), "ground probe normal should remain finite around degenerate triangles");
    ok &= expectNear(ground.point.y, 0.0f, 0.05f, "ground probe should ignore the raised zero-area triangle");
    ok &= expect(ground.normal.y > 0.99f, "ground probe should keep the valid floor normal");
    return ok;
}

bool kccStepsOntoThinTrimeshStep()
{
    const WorldTriMesh step = makeThinSingleStep();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&step, 1),
    };

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};

    PlayerVisState state;
    state.grounded = true;

    glm::vec3 pos{0.0f, 30.0f, -24.0f};
    glm::vec3 vel{0.0f, 0.0f, 480.0f};

    systems::runKinematicCharacterController(pos, vel, shape, state, 0.1f, world, entt::null, false);

    bool ok = true;
    ok &= expect(pos.z > 20.0f, "KCC should carry the player horizontally onto the step tread");
    ok &= expectNear(pos.y, 46.03125f, 0.1f, "KCC should settle the full capsule onto the raised tread");
    ok &= expect(state.grounded, "KCC should remain grounded after stepping onto a thin trimesh tread");
    ok &= expect(state.groundNormal.y > 0.99f, "KCC should keep the tread ground normal stable");
    return ok;
}

bool kccAscendsThinTrimeshStaircaseSmoothly()
{
    const WorldTriMesh stairs = makeThinStaircase(5, 12.0f, 28.0f);
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&stairs, 1),
    };

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};

    PlayerVisState state;
    state.grounded = true;

    glm::vec3 pos{0.0f, 30.03125f, -40.0f};
    glm::vec3 vel{0.0f, 0.0f, 260.0f};
    float maxFrameRise = 0.0f;
    float maxFrameDrop = 0.0f;
    float minForwardDelta = 1e30f;
    bool groundedEveryFrame = true;

    for (int frame = 0; frame < 80; ++frame) {
        const glm::vec3 before = pos;
        vel = {0.0f, 0.0f, 260.0f};
        systems::runKinematicCharacterController(pos, vel, shape, state, 1.0f / 128.0f, world, entt::null, false);
        maxFrameRise = std::max(maxFrameRise, pos.y - before.y);
        maxFrameDrop = std::max(maxFrameDrop, before.y - pos.y);
        minForwardDelta = std::min(minForwardDelta, pos.z - before.z);
        groundedEveryFrame &= state.grounded;
    }

    bool ok = true;
    ok &= expect(pos.z > 120.0f, "KCC should make continuous forward progress up authored thin stairs");
    ok &= expectNear(pos.y, 90.03125f, 0.5f, "KCC should settle on the top stair tread without overshoot");
    ok &= expect(maxFrameRise <= 13.0f, "KCC should not launch upward faster than one thin step per tick");
    ok &= expect(maxFrameDrop <= 0.25f, "KCC should not jitter downward while ascending thin stairs");
    ok &= expect(minForwardDelta > 0.25f, "KCC should not get stuck on a thin stair riser");
    ok &= expect(groundedEveryFrame, "KCC should remain grounded while walking up thin stairs");
    return ok;
}

bool kccAscendsThinTrimeshStaircaseAtSprintSpeed()
{
    const WorldTriMesh stairs = makeThinStaircase(20, 12.0f, 28.0f);
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&stairs, 1),
    };

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};

    PlayerVisState state;
    state.grounded = true;

    glm::vec3 pos{0.0f, 30.03125f, -40.0f};
    glm::vec3 vel{0.0f, 0.0f, 720.0f};
    float maxFrameRise = 0.0f;
    float minForwardDelta = 1e30f;
    bool groundedEveryFrame = true;

    for (int frame = 0; frame < 80; ++frame) {
        const glm::vec3 before = pos;
        vel = {0.0f, 0.0f, 720.0f};
        systems::runKinematicCharacterController(pos, vel, shape, state, 1.0f / 128.0f, world, entt::null, false);
        maxFrameRise = std::max(maxFrameRise, pos.y - before.y);
        minForwardDelta = std::min(minForwardDelta, pos.z - before.z);
        groundedEveryFrame &= state.grounded;
    }

    bool ok = true;
    ok &= expect(pos.z > 380.0f, "sprint-speed KCC should keep moving up authored thin stairs");
    ok &=
        expectNear(pos.y, 210.03125f, 0.75f, "sprint-speed KCC should settle on the expected stair without launching");
    ok &= expect(maxFrameRise <= 13.0f, "sprint-speed KCC should not rise by more than one thin step per frame");
    ok &= expect(minForwardDelta > 0.5f, "sprint-speed KCC should not stall on thin stair risers");
    ok &= expect(groundedEveryFrame, "sprint-speed KCC should remain grounded while ascending thin stairs");
    return ok;
}

bool thinCeilingPlaneBlocksUpwardCapsuleMotion()
{
    const WorldTriMesh ceiling = makeThinCeiling();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&ceiling, 1),
    };
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 4.0f, .up = {0.0f, 1.0f, 0.0f}};

    const HitResult hit = physics::sweepAll(capsule, {0.0f, 20.0f, 0.0f}, {0.0f, 48.0f, 0.0f}, world);

    bool ok = true;
    ok &= expect(hit.hit, "thin ceiling plane should block upward capsule motion");
    ok &= expect(hit.normal.y < -0.99f, "ceiling hit normal should push the capsule downward");
    ok &= expect(hit.tFirst > 0.0f && hit.tFirst < 1.0f, "ceiling hit should occur during the upward sweep");
    return ok;
}

bool airborneKccDoesNotSnapToSlopedCeiling()
{
    const WorldTriMesh ceiling = makeSlopedCeiling();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&ceiling, 1),
    };

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};

    PlayerVisState state;
    state.grounded = false;

    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    glm::vec3 vel{20.0f, 8.0f, 5.0f};

    systems::runKinematicCharacterController(pos, vel, shape, state, 1.0f / 128.0f, world, entt::null, false);

    bool ok = true;
    ok &= expect(finiteVec3(pos), "sloped ceiling snap regression should keep position finite");
    ok &= expect(pos.y < 1.0f, "airborne KCC should not snap upward to the underside of a sloped ceiling");
    ok &= expect(!state.grounded, "underside of a sloped ceiling should not classify as grounded support");
    return ok;
}

bool airborneKccDoesNotGainLiftFromSeparatingSlopedCeiling()
{
    const WorldTriMesh ceiling = makeShallowSlopedCeiling();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&ceiling, 1),
    };

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};

    PlayerVisState state;
    state.grounded = false;

    constexpr float slope = 0.049f;
    constexpr float dt = 1.0f / 128.0f;
    const glm::vec3 ceilingNormal = glm::normalize(glm::vec3{0.0f, -1.0f, -slope});
    const CapsuleShape capsule{.radius = shape.radius, .halfHeight = shape.halfHeight, .up = {0.0f, 1.0f, 0.0f}};
    const float support = capsule.minkowskiExtent(ceilingNormal);

    glm::vec3 pos{0.0f, 180.0f + (support + 0.05f) / ceilingNormal.y, 0.0f};
    glm::vec3 vel{44.2f, 18.9f, -546.3f};
    const glm::vec3 beforePos = pos;
    const glm::vec3 beforeVel = vel;

    const physics::KccFrameResult result =
        systems::runKinematicCharacterController(pos, vel, shape, state, dt, world, entt::null, false);

    bool ok = true;
    ok &=
        expect(glm::dot(beforeVel, ceilingNormal) > 0.0f, "test setup should move away from the sloped ceiling plane");
    ok &= expect(!result.hitCeiling, "separating diagonal motion should not be turned into a ceiling hit");
    ok &= expect(vel.y <= beforeVel.y + 0.001f, "sloped ceiling contact should not add upward velocity");
    ok &= expect(pos.y <= beforePos.y + beforeVel.y * dt + 0.01f,
                 "separating sloped ceiling should not add extra vertical displacement");
    ok &= expect(!state.grounded, "sloped ceiling underside should not ground the player");
    return ok;
}

bool rampGroundProbeKeepsAuthoredNormal()
{
    const WorldTriMesh ramp = makeThinRamp();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&ramp, 1),
    };
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 4.0f, .up = {0.0f, 1.0f, 0.0f}};

    const GroundProbeResult ground = physics::probeGround(capsule, {0.0f, 46.0f, 32.0f}, 40.0f, world);

    bool ok = true;
    ok &= expect(ground.hit, "ground probe should hit a thin authored ramp surface");
    ok &= expect(ground.walkable, "26 degree ramp should classify as walkable");
    ok &= expectNear(ground.normal.x, 0.0f, 0.02f, "ramp normal should not acquire sideways noise");
    ok &= expectNear(ground.normal.y, 0.894427f, 0.02f, "ramp normal should preserve authored slope");
    ok &= expectNear(ground.normal.z, -0.447214f, 0.02f, "ramp normal should point against the ramp rise");
    return ok;
}

bool sweptCapsuleHitsFiniteWallEdge()
{
    const WorldTriMesh wall = makeThinWall();
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 4.0f, .up = {0.0f, 1.0f, 0.0f}};

    // The capsule center stays within the infinite wall plane's radius slab
    // for the whole motion, but starts outside the finite quad's side edge.
    // Plane-only CCD misses this; capsule-vs-triangle CCD must hit the edge.
    const glm::vec3 start{8.0f, 20.0f, 36.0f};
    const glm::vec3 end{8.0f, 20.0f, 0.0f};

    const HitResult hit = physics::sweepCapsuleVsTriMesh(capsule, start, end, wall);
    bool ok = true;
    ok &= expect(hit.hit, "swept capsule should hit a finite thin-wall edge");
    ok &= expect(hit.tFirst > 0.0f && hit.tFirst < 0.5f, "edge hit should occur early in the sweep");
    ok &= expect(hit.normal.x > 0.6f, "edge hit normal should push away from the wall plane");
    ok &= expect(hit.normal.z > 0.3f, "edge hit normal should include the finite-edge direction");
    return ok;
}

bool staticCapsuleDepenUsesSurfaceFeatureNormal()
{
    const WorldTriMesh wall = makeThinWall();
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 4.0f, .up = {0.0f, 1.0f, 0.0f}};

    const glm::vec3 pos{8.0f, 20.0f, 24.0f};
    const glm::vec3 vel{0.0f};

    const DepenContact contact = physics::deepestCapsuleContactVsTriMesh(capsule, pos, vel, wall);
    const float expectedDepth = 10.0f - std::sqrt(8.0f * 8.0f + 4.0f * 4.0f);

    bool ok = true;
    ok &= expect(contact.valid, "capsule overlapping a thin-wall edge should produce a depen contact");
    ok &= expectNear(
        contact.depth, expectedDepth, 0.05f, "edge depen depth should be based on segment-triangle distance");
    ok &= expect(contact.normal.x > 0.8f, "edge depen normal should push away from the wall plane");
    ok &= expect(contact.normal.z > 0.35f, "edge depen normal should push away from the finite edge");
    return ok;
}

bool twoTriangleFloorHasStableSurfaceOverlap()
{
    const WorldTriMesh floor = makeTwoTriangleFloor();
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 4.0f, .up = {0.0f, 1.0f, 0.0f}};

    const glm::vec3 pos{0.0f, 13.75f, 0.0f};
    const glm::vec3 vel{0.0f};

    const DepenContact contact = physics::deepestCapsuleContactVsTriMesh(capsule, pos, vel, floor);
    bool ok = true;
    ok &= expect(contact.valid, "capsule slightly penetrating a two-triangle floor should be recovered");
    ok &= expectNear(contact.depth, 0.25f, 0.05f, "floor depen should use capsule-surface penetration depth");
    ok &= expect(contact.normal.y > 0.99f, "floor depen normal should remain stable across the internal seam");
    return ok;
}

bool sphereCastAgainstTriMeshUsesSurfaceFeatureNormal()
{
    const WorldTriMesh wall = makeThinWall();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    // Same geometry as the capsule CCD test, reduced to a sphere. The sphere
    // starts outside the finite wall edge, even though it is inside the
    // infinite wall plane's radius slab. AABB-expanded trimesh casts miss this
    // or report a face-only normal; sphere/trimesh must use the edge feature.
    const SphereHitResult hit = physics::sphereCast(10.0f, {8.0f, 20.0f, 36.0f}, {8.0f, 20.0f, 0.0f}, world);

    bool ok = true;
    ok &= expect(hit.hit, "sphere cast should hit a finite thin-wall edge");
    ok &= expect(hit.t > 0.0f && hit.t < 0.5f, "sphere edge hit should occur early in the cast");
    ok &= expect(hit.normal.x > 0.6f, "sphere edge normal should push away from the wall plane");
    ok &= expect(hit.normal.z > 0.3f, "sphere edge normal should include the finite-edge direction");
    return ok;
}

bool staticBroadphaseReturnsOnlyOverlappingTriMeshes()
{
    std::array<WorldTriMesh, 2> meshes{
        makeThinWall(),
        makeCookedMesh(
            {
                {1000.0f, 0.0f, -20.0f},
                {1000.0f, 40.0f, -20.0f},
                {1000.0f, 40.0f, 20.0f},
                {1000.0f, 0.0f, 20.0f},
            },
            {
                0,
                1,
                2,
                0,
                2,
                3,
            }),
    };

    StaticWorldBroadphase broadphase;
    physics::buildStaticWorldBroadphase(broadphase, meshes);

    std::vector<uint32_t> hits;
    physics::queryStaticWorldBroadphase(
        broadphase, WorldAABB{.min = {-20.0f, -1.0f, -30.0f}, .max = {20.0f, 50.0f, 30.0f}}, [&](uint32_t meshIndex) {
            hits.push_back(meshIndex);
            return true;
        });

    bool ok = true;
    ok &= expect(hits.size() == 1, "static broadphase should cull non-overlapping mesh bounds");
    ok &= expect(!hits.empty() && hits.front() == 0u, "static broadphase should return the overlapping mesh index");
    return ok;
}

bool wallDetectionTracksOverlappingThinTriMeshWall()
{
    const WorldTriMesh wall = makeThinWall();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    // The player is already inside the wall probe radius. A pure swept sphere
    // cast can miss this because it starts overlapped with the inflated
    // wall. Wallrun attachment should instead use a segment/capsule
    // closest-point query and keep the mesh/triangle identity stable.
    const physics::WallDetectionResult result =
        physics::detectWalls({-8.0f, 20.0f, 0.0f}, 0.0f, {10.0f, 30.0f, 10.0f}, world, 24.0f, 10.0f);

    bool ok = true;
    ok &= expect(result.wallRight, "wall detection should keep an overlapping thin trimesh wall attached");
    ok &= expect(result.rightNormal.x < -0.9f, "right wall normal should point from the wall toward the player");
    ok &= expectNear(result.rightPoint.x, 0.0f, 0.05f, "right wall point should lie on the authored wall surface");
    ok &= expect(result.rightMeshIndex == 0u, "right wall detection should report the trimesh index");
    ok &= expect(result.rightTriId != UINT32_MAX, "right wall detection should report the triangle id");
    return ok;
}

bool wallDetectionGroundDistanceUsesFlippedGravityDirection()
{
    const WorldTriMesh ceiling = makeThinCeiling();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&ceiling, 1),
    };

    const physics::WallDetectionResult result = physics::detectWalls(
        {0.0f, 0.0f, 0.0f}, 0.0f, {10.0f, 30.0f, 10.0f}, world, 24.0f, 10.0f, glm::vec3{0.0f}, true);

    bool ok = true;
    ok &= expect(result.groundDistance < 20.0f, "flipped gravity ground distance should probe toward +Y");
    ok &= expect(result.groundDistance > 8.0f, "flipped gravity ground distance should report surface distance");
    return ok;
}

bool wallAttachmentLookaheadFindsOuterCornerContinuation()
{
    std::array<WorldTriMesh, 2> meshes{makeThinWall(), makeThinDepthWallAt(20.0f)};
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 20.0f, .up = {0.0f, 1.0f, 0.0f}};

    const physics::WallAttachmentResult result = physics::findWallRunAttachment(
        capsule, {-10.0f, 20.0f, 8.0f}, world, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, 12.0f, 24.0f);

    bool ok = true;
    ok &= expect(result.found, "wallrun lookahead should find an outer-corner continuation wall");
    ok &= expect(result.meshIndex == 1u, "wallrun lookahead should prefer the upcoming outside-corner wall");
    ok &= expect(result.normal.z < -0.9f, "outside-corner wall normal should preserve forward wallrun continuation");
    return ok;
}

bool wallAttachmentWalksSingleMeshAdjacencyAtCorner()
{
    const WorldTriMesh mesh = makeSingleMeshOutsideWallCorner();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&mesh, 1),
    };
    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 20.0f, .up = {0.0f, 1.0f, 0.0f}};

    const physics::WallAttachmentResult result = physics::findWallRunAttachment(capsule,
                                                                                {-10.0f, 20.0f, 18.0f},
                                                                                world,
                                                                                {-1.0f, 0.0f, 0.0f},
                                                                                {0.0f, 0.0f, 1.0f},
                                                                                0.0f,
                                                                                24.0f,
                                                                                0u,
                                                                                1u,
                                                                                physics::TriRegion::Edge1);

    bool ok = true;
    ok &= expect(result.found, "wallrun adjacency should find a single-mesh outside-corner continuation");
    ok &= expect(result.meshIndex == 0u, "wallrun adjacency should stay on the same cooked mesh");
    ok &= expect(result.triId == 2u, "wallrun adjacency should hop to the neighbour triangle across the seam");
    ok &= expect(result.normal.z < -0.9f, "single-mesh corner continuation should use the new wall normal");
    return ok;
}

bool wallrunOuterCornerKeepsForwardVelocity()
{
    std::array<WorldTriMesh, 2> meshes{makeThinWall(), makeThinDepthWallAt(20.0f)};
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-10.0f, 20.0f, 16.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {0.0f, 20.0f, 16.0f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 1u;
    sim.wallRegion = physics::TriRegion::Edge1;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(outVis.moveMode == MoveMode::WallRunning, "outer corner should continue wallrun");
    ok &= expect(outSim.wallNormal.z < -0.7f, "outer corner should hand off to the forward continuation wall");
    ok &= expect(glm::dot(glm::vec3{outVel.value.x, 0.0f, outVel.value.z}, outSim.wallForward) >= 0.0f,
                 "outer corner handoff should not reverse horizontal velocity");
    return ok;
}

bool wallrunSingleMeshExternalLCornerContinuesAroundCorner()
{
    const WorldTriMesh mesh = makeSingleMeshOutsideWallCorner();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&mesh, 1),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-10.0f, 20.0f, 16.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {0.0f, 20.0f, 16.0f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 1u;
    sim.wallRegion = physics::TriRegion::Edge1;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    bool wallrunningEveryFrame = true;
    float minHorizSpeed = 1e30f;
    for (int frame = 0; frame < 16; ++frame) {
        systems::runMovement(registry, 1.0f / 128.0f, world);
        systems::runKinematicCharacterController(registry.get<Position>(player).value,
                                                 registry.get<Velocity>(player).value,
                                                 registry.get<CollisionShape>(player),
                                                 registry.get<PlayerVisState>(player),
                                                 1.0f / 128.0f,
                                                 world,
                                                 player,
                                                 registry.get<PlayerSimState>(player).jumpedThisTick,
                                                 &registry.get<PlayerSimState>(player));

        const auto& stepVis = registry.get<PlayerVisState>(player);
        const auto& stepVel = registry.get<Velocity>(player);
        wallrunningEveryFrame &= stepVis.moveMode == MoveMode::WallRunning;
        minHorizSpeed = std::min(minHorizSpeed, glm::length(glm::vec3{stepVel.value.x, 0.0f, stepVel.value.z}));
    }

    const auto& outPos = registry.get<Position>(player);
    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(wallrunningEveryFrame, "external L corner should not drop wallrun during the handoff");
    ok &= expect(outVis.moveMode == MoveMode::WallRunning, "external L corner should still be wallrunning");
    ok &= expect(outSim.wallNormal.z < -0.7f, "external L corner should attach to the perpendicular wall");
    ok &= expect(outSim.wallForward.x < -0.7f, "external L corner should continue around the outside corner");
    ok &= expect(outPos.value.x < -20.0f, "external L corner should move along the new wall");
    ok &= expect(minHorizSpeed > 300.0f, "external L corner should not stall during transition");
    ok &= expect(outVel.value.x < -300.0f, "external L corner should preserve speed into the new wall direction");
    return ok;
}

bool wallrunMapExternalLCornerContinuesWithKcc()
{
    std::array<WorldTriMesh, 2> meshes = makeAuthoredMapExternalLCornerSegment();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{671.530f, 80.0f, 577.639f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {687.566f, 80.0f, 577.639f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    bool wallrunningEveryFrame = true;
    float minHorizSpeed = 1e30f;
    for (int frame = 0; frame < 24; ++frame) {
        runMovementAndKccTick(registry, player, world, 1.0f / 128.0f);

        const auto& stepVis = registry.get<PlayerVisState>(player);
        const auto& stepVel = registry.get<Velocity>(player);
        wallrunningEveryFrame &= stepVis.moveMode == MoveMode::WallRunning;
        minHorizSpeed = std::min(minHorizSpeed, glm::length(glm::vec3{stepVel.value.x, 0.0f, stepVel.value.z}));
    }

    const auto& outPos = registry.get<Position>(player);
    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(wallrunningEveryFrame, "map external L corner should not drop during KCC simulation");
    ok &= expect(outVis.moveMode == MoveMode::WallRunning, "map external L corner should stay in wallrun");
    ok &= expect(outSim.wallNormal.z > 0.7f, "map external L corner should attach to the outside of the corridor wall");
    ok &= expect(outSim.wallForward.x > 0.7f, "map external L corner should redirect into the corridor");
    ok &= expect(outPos.value.x > 740.0f, "map external L corner should make progress along the corridor wall");
    ok &= expect(minHorizSpeed > 300.0f, "map external L corner should not stall during transition");
    ok &= expect(outVel.value.x > 300.0f, "map external L corner should preserve speed into the corridor direction");
    return ok;
}

bool wallrunExternalCornerDefersRedirectUntilOldWallClear()
{
    std::array<WorldTriMesh, 2> meshes = makeAuthoredMapExternalLCornerSegment();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{671.530f, 80.0f, 577.639f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {687.566f, 80.0f, 577.639f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(outSim.wallNormal.x < -0.7f,
                 "external corner should keep the old wall before the capsule clears the terminal edge");
    ok &= expect(outSim.wallCornerTransitionActive, "external corner should keep the next wall as a pending handoff");
    ok &= expect(outVel.value.z > 300.0f, "external corner should keep moving along the old wall before clearance");
    ok &= expect(std::abs(outVel.value.x) < 50.0f,
                 "external corner should not redirect into the old wall before clearance");
    return ok;
}

bool wallrunGapDropsWithoutReversingVelocity()
{
    const WorldTriMesh wall = makeThinWallSegment(-60.0f, 0.0f);
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-10.0f, 20.0f, 8.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {0.0f, 20.0f, 0.0f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 1u;
    sim.wallRegion = physics::TriRegion::Edge1;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(outVis.moveMode == MoveMode::OnFoot,
                 "wallrun should drop when a wall segment ends without continuation");
    ok &= expect(outVel.value.z > 0.0f, "wallrun drop should preserve forward velocity instead of reversing");
    return ok;
}

bool wallrunMapCorridorTurnsIntoCorridor()
{
    std::array<WorldTriMesh, 2> meshes = makeAuthoredMapExternalLCornerSegment();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{671.530f, 80.0f, 577.639f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {687.566f, 80.0f, 577.639f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(outVis.moveMode == MoveMode::WallRunning,
                 "map corridor outside corner should keep wallrunning while preparing the turn");
    ok &= expect(outSim.wallNormal.x < -0.7f,
                 "map corridor handoff should keep the approach wall until the capsule clears the edge");
    ok &= expect(outSim.wallCornerTransitionActive,
                 "map corridor handoff should store a pending external-corner transition");
    ok &= expect(outVel.value.z > 300.0f,
                 "map corridor handoff should keep moving along the approach wall before clearance");
    ok &= expect(glm::length(glm::vec3{outVel.value.x, 0.0f, outVel.value.z}) > 300.0f,
                 "map corridor handoff should preserve moving wallrun speed instead of stopping in place");
    return ok;
}

bool wallrunInternalCornerKeepsMoving()
{
    WorldTriMesh corner = makeCookedMesh(
        {
            {0.0f, 0.0f, -80.0f},
            {0.0f, 40.0f, -80.0f},
            {0.0f, 40.0f, 20.0f},
            {0.0f, 0.0f, 20.0f},
            {80.0f, 0.0f, 20.0f},
            {80.0f, 40.0f, 20.0f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
            3,
            2,
            5,
            3,
            5,
            4,
        });
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&corner, 1),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-10.0f, 20.0f, 12.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {0.0f, 20.0f, 12.0f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 1u;
    sim.wallRegion = physics::TriRegion::Edge1;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    const CapsuleShape capsule{.radius = 10.0f, .halfHeight = 20.0f, .up = {0.0f, 1.0f, 0.0f}};
    const physics::WallAttachmentResult attachment = physics::findWallRunAttachment(capsule,
                                                                                    {-10.0f, 20.0f, 12.0f},
                                                                                    world,
                                                                                    {-1.0f, 0.0f, 0.0f},
                                                                                    {0.0f, 0.0f, 1.0f},
                                                                                    10.0f,
                                                                                    35.0f,
                                                                                    0u,
                                                                                    1u,
                                                                                    physics::TriRegion::Edge1);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(attachment.found, "internal corner attachment query should find a candidate");
    ok &= expect(attachment.normal.z < -0.7f, "internal corner attachment query should prefer the turned wall");
    ok &= expect(outVis.moveMode == MoveMode::WallRunning, "internal corner should stay wallrunning");
    ok &= expect(outSim.wallNormal.z < -0.7f || outSim.wallCornerTransitionActive,
                 "internal corner should either transition immediately or queue a clearance-safe turn");
    ok &= expect(glm::length(glm::vec3{outVel.value.x, 0.0f, outVel.value.z}) > 300.0f,
                 "internal corner wallrun should keep moving instead of becoming static");
    return ok;
}

bool wallrunCapturedBalconyCornerDoesNotDropIntoJitter()
{
    std::array<WorldTriMesh, 2> meshes = makeCapturedBalconyDiagonalCornerSegment();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{227.053f, -22.001f, -867.158f});
    registry.emplace<Velocity>(player, glm::vec3{-565.685f, 75.743f, 565.686f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = glm::normalize(glm::vec3{-1.0f, 0.0f, -1.0f});
    sim.wallForward = glm::normalize(glm::vec3{-1.0f, 0.0f, 1.0f});
    sim.wallAnchor = {238.389f, -22.001f, -855.822f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.left = true;
    registry.emplace<InputSnapshot>(player, input);

    bool droppedFromWallrun = false;
    bool reattachedAfterDrop = false;
    float maxReverseOrDepenStep = 0.0f;
    float maxPostImpactStep = 0.0f;
    glm::vec3 firstBlockedPos{0.0f};
    bool sawBlockedFrame = false;
    glm::vec3 previousPos = registry.get<Position>(player).value;
    for (int frame = 0; frame < 80; ++frame) {
        input.yaw = frame < 2 ? -0.615024f : (frame < 4 ? -0.625524f : (frame < 6 ? -0.642324f : -0.648624f));
        registry.replace<InputSnapshot>(player, input);

        const physics::KccFrameResult kcc = runMovementAndKccTick(registry, player, world, 1.0f / 128.0f);

        const auto& stepPos = registry.get<Position>(player);
        const auto& stepVis = registry.get<PlayerVisState>(player);
        const auto& stepSim = registry.get<PlayerSimState>(player);
        const glm::vec3 stepDelta = stepPos.value - previousPos;
        maxReverseOrDepenStep = std::max(maxReverseOrDepenStep, std::max(0.0f, -glm::dot(stepDelta, sim.wallForward)));
        if (stepSim.wallrunBlockerActive || kcc.hitBlocker) {
            if (!sawBlockedFrame) {
                sawBlockedFrame = true;
                firstBlockedPos = stepPos.value;
            } else {
                maxPostImpactStep = std::max(maxPostImpactStep, glm::length(glm::vec3{stepDelta.x, 0.0f, stepDelta.z}));
            }
        }
        if (stepVis.moveMode != MoveMode::WallRunning)
            droppedFromWallrun = true;
        else if (droppedFromWallrun)
            reattachedAfterDrop = true;
        previousPos = stepPos.value;
    }

    const auto& outPos = registry.get<Position>(player);
    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);

    bool ok = true;
    ok &= expect(sawBlockedFrame, "captured balcony corner should classify the obstruction as a wallrun blocker");
    ok &= expect(outVis.moveMode == MoveMode::WallRunning,
                 "captured balcony blocker should hold wallrun while the original wall remains valid");
    ok &= expect(outSim.wallrunBlockerActive, "captured balcony blocker should remain latched while input is held");
    ok &= expect(!outVis.exitingWall, "captured balcony blocker hold should not start a global reattach cooldown");
    ok &= expect(!reattachedAfterDrop, "captured balcony corner should not reattach into a low-speed jitter loop");
    ok &= expect(maxReverseOrDepenStep < 1.0f,
                 "captured balcony corner should not alternate between depenetration positions");
    ok &= expect(maxPostImpactStep < 0.5f,
                 "captured balcony corner should settle at the obstruction instead of ping-ponging");
    ok &= expect(glm::length(glm::vec3{outPos.value.x - firstBlockedPos.x, 0.0f, outPos.value.z - firstBlockedPos.z}) <
                     0.75f,
                 "captured balcony corner should stay settled while the wallrun input is held");
    return ok;
}

bool wallrunBlockedCornerTransitionDoesNotTimeoutIntoCooldownLoop()
{
    std::array<WorldTriMesh, 2> meshes = makeCapturedBalconyDiagonalCornerSegment();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{189.932f, -6.714f, -830.036f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 7.812f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {173.960f, -6.714f, -830.036f};
    sim.wallMeshIndex = 1u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    sim.wallCornerTransitionActive = true;
    sim.wallCornerAnchor = {173.960f, -6.714f, -830.036f};
    sim.wallCornerFromNormal = {1.0f, 0.0f, 0.0f};
    sim.wallCornerFromForward = {0.0f, 0.0f, 1.0f};
    sim.wallCornerToNormal = glm::normalize(glm::vec3{-1.0f, 0.0f, -1.0f});
    sim.wallCornerToForward = glm::normalize(glm::vec3{-1.0f, 0.0f, 1.0f});
    sim.wallCornerMeshIndex = 0u;
    sim.wallCornerTriId = 0u;
    sim.wallCornerRegion = physics::TriRegion::Face;
    sim.wallCornerTimer = 0.23f;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = -0.999150f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &=
        expect(outVis.moveMode == MoveMode::OnFoot, "blocked slow wallrun corner should drop instead of staying stuck");
    ok &= expect(!outVis.exitingWall, "blocked slow wallrun corner should not start an exit-wall cooldown");
    ok &= expect(outSim.wallBlacklistActive, "blocked slow wallrun corner should remember the exited wall normal");
    ok &= expect(glm::length(glm::vec3{outVel.value.x, 0.0f, outVel.value.z}) < 100.0f,
                 "blocked slow wallrun corner should not accelerate back above stay speed while dropping");
    return ok;
}

bool wallrunRequiresMinimumAttachSpeed()
{
    const WorldTriMesh wall = makeThinWall();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-8.0f, 20.0f, 0.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 199.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::OnFoot;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);
    registry.emplace<PlayerSimState>(player);

    InputSnapshot input;
    input.jump = true;
    input.left = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    bool ok = true;
    ok &= expect(registry.get<PlayerVisState>(player).moveMode == MoveMode::OnFoot,
                 "wallrun should not attach below the minimum entry speed");
    ok &= expect(!registry.get<PlayerSimState>(player).wallAttachmentValid,
                 "failed low-speed wallrun entry should not seed wall attachment state");
    return ok;
}

bool wallrunDropsBelowMinimumStaySpeedWithoutCooldown()
{
    const WorldTriMesh wall = makeThinWall();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-8.0f, 20.0f, 0.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 99.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {0.0f, 20.0f, 0.0f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 1u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);

    bool ok = true;
    ok &=
        expect(outVis.moveMode == MoveMode::OnFoot, "wallrun should drop once horizontal speed falls below stay speed");
    ok &= expect(!outVis.exitingWall, "speed-based wallrun drop should not start a global reattach cooldown");
    ok &= expect(outSim.wallBlacklistActive, "speed-based wallrun drop should retain same-wall blacklist context");
    return ok;
}

bool wallrunCanAttachToDifferentWallImmediatelyAfterExit()
{
    const WorldTriMesh wall = makeThinWall();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-8.0f, 20.0f, 0.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 250.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::OnFoot;
    vis.grounded = false;
    vis.exitingWall = true;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.exitWallTimer = tms::k_wallrunExitTime;
    sim.wallBlacklistActive = true;
    sim.wallBlacklistNormal = {0.0f, 0.0f, -1.0f};
    sim.wallBlacklistHeight = 20.0f;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.left = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    bool ok = true;
    ok &= expect(registry.get<PlayerVisState>(player).moveMode == MoveMode::WallRunning,
                 "wallrun should attach to a different wall immediately after leaving another wall");
    ok &= expect(registry.get<PlayerSimState>(player).wallAttachmentValid,
                 "instant different-wall reattach should seed wall attachment state");
    return ok;
}

bool wallrunSameWallBlacklistIsSpeedBased()
{
    const WorldTriMesh wall = makeThinWall();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    auto runCase = [&](glm::vec3 velocity) {
        Registry registry;
        const entt::entity player = registry.create();
        registry.emplace<Position>(player, glm::vec3{-8.0f, 20.0f, 0.0f});
        registry.emplace<Velocity>(player, velocity);

        CollisionShape shape;
        shape.type = CollisionShapeType::Capsule;
        shape.radius = 10.0f;
        shape.halfHeight = 20.0f;
        shape.halfExtents = {10.0f, 30.0f, 10.0f};
        registry.emplace<CollisionShape>(player, shape);

        PlayerVisState vis;
        vis.moveMode = MoveMode::OnFoot;
        vis.grounded = false;
        registry.emplace<PlayerVisState>(player, vis);

        PlayerSimState sim;
        sim.wallBlacklistActive = true;
        sim.wallBlacklistNormal = {-1.0f, 0.0f, 0.0f};
        sim.wallBlacklistHeight = 20.0f;
        registry.emplace<PlayerSimState>(player, sim);

        InputSnapshot input;
        input.jump = true;
        input.left = true;
        input.yaw = 0.0f;
        registry.emplace<InputSnapshot>(player, input);

        systems::runMovement(registry, 1.0f / 128.0f, world);
        return registry.get<PlayerVisState>(player).moveMode;
    };

    bool ok = true;
    ok &= expect(runCase(glm::vec3{-250.0f, 0.0f, 0.0f}) == MoveMode::OnFoot,
                 "same-wall blacklist should block reattach while moving away from that wall");
    ok &= expect(runCase(glm::vec3{250.0f, 0.0f, 0.0f}) == MoveMode::WallRunning,
                 "same-wall blacklist should allow reattach once moving back into that wall fast enough");
    return ok;
}

bool wallrunRollScalesWithViewAngle()
{
    std::array<WorldTriMesh, 3> meshes = makeAuthoredMapWallrunCorridorSegment();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    auto runAtYaw = [&](float yaw) {
        Registry registry;
        const entt::entity player = registry.create();
        registry.emplace<Position>(player, glm::vec3{671.530f, 80.0f, 1200.0f});
        registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

        CollisionShape shape;
        shape.type = CollisionShapeType::Capsule;
        shape.radius = 10.0f;
        shape.halfHeight = 20.0f;
        shape.halfExtents = {10.0f, 30.0f, 10.0f};
        registry.emplace<CollisionShape>(player, shape);

        PlayerVisState vis;
        vis.moveMode = MoveMode::WallRunning;
        vis.wallRunSide = WallSide::Right;
        vis.grounded = false;
        registry.emplace<PlayerVisState>(player, vis);

        PlayerSimState sim;
        sim.wallNormal = {-1.0f, 0.0f, 0.0f};
        sim.wallForward = {0.0f, 0.0f, 1.0f};
        sim.wallAnchor = {687.566f, 80.0f, 1200.0f};
        sim.wallMeshIndex = 0u;
        sim.wallTriId = 0u;
        sim.wallRegion = physics::TriRegion::Face;
        sim.wallAttachmentValid = true;
        registry.emplace<PlayerSimState>(player, sim);

        InputSnapshot input;
        input.jump = true;
        input.forward = true;
        input.yaw = yaw;
        registry.emplace<InputSnapshot>(player, input);

        systems::runMovement(registry, 1.0f / 128.0f, world);
        return registry.get<PlayerVisState>(player).targetCameraTilt;
    };

    const float parallelRoll = runAtYaw(0.0f);
    const float wallFacingRoll = runAtYaw(1.57079632679f);

    bool ok = true;
    ok &= expect(parallelRoll > tms::k_wallrunCameraTilt * 0.9f,
                 "wallrun roll should be strongest when looking along the wall");
    ok &= expect(std::abs(wallFacingRoll) < std::abs(parallelRoll) * 0.5f,
                 "wallrun roll should soften when looking toward or away from the wall normal");
    return ok;
}

bool wallrunMapCorridorEndDoesNotAttachToAirSide()
{
    std::array<WorldTriMesh, 3> meshes = makeAuthoredMapWallrunCorridorSegment();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{703.597f, 80.0f, 577.639f});
    registry.emplace<Velocity>(player, glm::vec3{-500.0f, 0.0f, 0.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {0.0f, 0.0f, -1.0f};
    sim.wallForward = {-1.0f, 0.0f, 0.0f};
    sim.wallAnchor = {703.597f, 80.0f, 593.671f};
    sim.wallMeshIndex = 1u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = -1.57079632679f;
    registry.emplace<InputSnapshot>(player, input);

    for (int i = 0; i < 12; ++i)
        systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    if (outVis.moveMode == MoveMode::WallRunning) {
        ok &= expect(outSim.wallNormal.x < 0.7f,
                     "corridor end should not attach to the east/air side of the main-room wall");
        ok &= expect(outVel.value.x <= 0.0f, "corridor end should not reverse back into the corridor");
    }
    return ok;
}

bool wallrunSolidEndcapSlidesAsBlockerWithoutWrongWallHandoff()
{
    std::array<WorldTriMesh, 2> meshes = makeCapturedBalconyDiagonalCornerSegment();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{227.053f, -22.001f, -867.158f});
    registry.emplace<Velocity>(player, glm::vec3{-565.685f, 75.743f, 565.686f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    const glm::vec3 originalWallNormal = glm::normalize(glm::vec3{-1.0f, 0.0f, -1.0f});
    PlayerSimState sim;
    sim.wallNormal = originalWallNormal;
    sim.wallForward = glm::normalize(glm::vec3{-1.0f, 0.0f, 1.0f});
    sim.wallAnchor = {238.389f, -22.001f, -855.822f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.left = true;
    registry.emplace<InputSnapshot>(player, input);

    bool sawBlocker = false;
    bool exhaustedAfterBlocker = false;
    bool wrongWallHandoff = false;
    bool droppedAfterBlocker = false;
    float maxReverseStep = 0.0f;
    glm::vec3 previousPos = registry.get<Position>(player).value;

    for (int frame = 0; frame < 64; ++frame) {
        input.yaw = frame < 2 ? -0.615024f : (frame < 4 ? -0.625524f : (frame < 6 ? -0.642324f : -0.648624f));
        registry.replace<InputSnapshot>(player, input);

        const physics::KccFrameResult kcc = runMovementAndKccTick(registry, player, world, 1.0f / 128.0f);
        const auto& stepPos = registry.get<Position>(player);
        const auto& stepVis = registry.get<PlayerVisState>(player);
        const auto& stepSim = registry.get<PlayerSimState>(player);

        const glm::vec3 delta = stepPos.value - previousPos;
        maxReverseStep = std::max(maxReverseStep, std::max(0.0f, -glm::dot(delta, sim.wallForward)));
        previousPos = stepPos.value;

        sawBlocker = sawBlocker || stepSim.wallrunBlockerActive || kcc.hitBlocker;
        if (sawBlocker && kcc.caExhausted)
            exhaustedAfterBlocker = true;
        if (sawBlocker && stepVis.moveMode != MoveMode::WallRunning)
            droppedAfterBlocker = true;
        if (sawBlocker && glm::dot(stepSim.wallNormal, originalWallNormal) < 0.9f)
            wrongWallHandoff = true;
    }

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(sawBlocker, "solid endcap wallrun should classify the obstacle as a blocker");
    ok &= expect(outVis.moveMode == MoveMode::WallRunning, "solid endcap blocker should keep wallrun mode attached");
    ok &= expect(outSim.wallrunBlockerActive, "solid endcap should latch blocker state while held against it");
    ok &= expect(!wrongWallHandoff, "solid endcap blocker should not become the wallrun attachment wall");
    ok &= expect(!droppedAfterBlocker, "solid endcap blocker should slide or hold instead of dropping");
    ok &= expect(!exhaustedAfterBlocker, "blocker reconciliation should prevent repeated KCC exhaustion");
    ok &= expect(maxReverseStep < 1.0f, "blocker reconciliation should not ping-pong backward along the wall");
    ok &= expect(glm::length(glm::vec3{outVel.value.x, 0.0f, outVel.value.z}) < tms::k_wallrunMaxSpeed + 1.0f,
                 "blocker slide should preserve bounded horizontal wallrun velocity");
    return ok;
}

bool wallrunDisconnectedOctagonFrontFaceContinuesInsteadOfDropping()
{
    std::array<WorldTriMesh, 2> meshes = makeDisconnectedOctagonFaceTransition();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{413.889f, -36.136f, -1073.429f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 146.507f, 425.815f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {429.920f, -36.136f, -1076.756f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Edge1;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.085659f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(outVis.moveMode == MoveMode::WallRunning,
                 "disconnected octagon face ahead should continue wallrun instead of dropping");
    ok &= expect(outSim.wallNormal.x < -0.5f && outSim.wallNormal.z < -0.5f,
                 "octagon transition should attach to the front continuation face");
    ok &= expect(glm::length(glm::vec3{outVel.value.x, 0.0f, outVel.value.z}) > 300.0f,
                 "octagon transition should preserve wallrun speed");
    return ok;
}

bool wallrunCapturedMapOctagonTransitionDoesNotDropAcrossKcc()
{
    std::array<WorldTriMesh, 2> meshes = makeCapturedMapOctagonFirstFaceTransition();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{413.889f, -43.124f, -1083.318f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 154.340f, 414.096f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {429.9f, -43.124f, -1083.318f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 1u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.085659f;
    registry.emplace<InputSnapshot>(player, input);

    bool wallrunningEveryFrame = true;
    float minHorizSpeed = 1e30f;
    for (int frame = 0; frame < 16; ++frame) {
        runMovementAndKccTick(registry, player, world, 1.0f / 128.0f);
        const auto& stepVis = registry.get<PlayerVisState>(player);
        const auto& stepVel = registry.get<Velocity>(player);
        wallrunningEveryFrame &= stepVis.moveMode == MoveMode::WallRunning;
        minHorizSpeed = std::min(minHorizSpeed, glm::length(glm::vec3{stepVel.value.x, 0.0f, stepVel.value.z}));
    }

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);
    const auto& outVel = registry.get<Velocity>(player);

    bool ok = true;
    ok &= expect(wallrunningEveryFrame, "captured map octagon transition should not drop during movement/KCC");
    ok &= expect(outVis.moveMode == MoveMode::WallRunning, "captured map octagon transition should stay wallrunning");
    ok &= expect(outSim.wallNormal.x < -0.5f && outSim.wallNormal.z < -0.5f,
                 "captured map octagon transition should attach to the diagonal continuation face");
    ok &= expect(glm::dot(glm::vec3{outVel.value.x, 0.0f, outVel.value.z}, outSim.wallForward) > 300.0f,
                 "captured map octagon transition should keep forward wallrun speed");
    ok &= expect(minHorizSpeed > 300.0f, "captured map octagon transition should not stall between faces");
    return ok;
}

bool wallrunKccContactOnContinuationFaceDoesNotBecomeBlocker()
{
    const glm::vec3 continuationNormal = glm::normalize(glm::vec3{1.0f, 0.0f, -1.0f});
    const WorldTriMesh continuationWall = makeCookedMesh(
        {
            {0.0f, -64.0f, 0.0f},
            {-160.0f, -64.0f, -160.0f},
            {-160.0f, 96.0f, -160.0f},
            {0.0f, 96.0f, 0.0f},
        },
        {
            0,
            1,
            2,
            0,
            2,
            3,
        });
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&continuationWall, 1),
    };

    glm::vec3 pos{-16.0f, 0.0f, -16.0f};
    glm::vec3 vel{0.0f, 110.0f, -590.0f};

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;

    PlayerSimState sim;
    sim.wallNormal = {1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, -1.0f};
    sim.wallAnchor = {-16.0f, 0.0f, 0.0f};
    sim.wallAttachmentValid = true;

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 3.14159265f;

    physics::KccFrameResult kcc;
    kcc.posBefore = pos;
    kcc.posAfter = pos + glm::vec3{0.0f, 0.0f, -1.0f};
    kcc.velBefore = vel;
    kcc.velAfter = vel;
    kcc.attemptedDelta = glm::vec3{0.0f, 0.0f, -4.6f};
    kcc.actualDelta = kcc.posAfter - kcc.posBefore;
    kcc.progressRatio = 0.2f;
    kcc.hitWall = true;
    kcc.hitBlocker = true;
    kcc.blockerNormal = continuationNormal;
    kcc.lastHitNormal = continuationNormal;

    systems::reconcileMovementAfterKcc(pos, vel, shape, vis, sim, input, world, kcc, 1.0f / 128.0f);

    bool ok = true;
    ok &= expect(vis.moveMode == MoveMode::WallRunning,
                 "KCC contact on a valid continuation face should keep wallrun mode");
    ok &= expect(!sim.wallrunBlockerActive, "valid continuation face should not be latched as a wallrun blocker");
    ok &= expect(glm::dot(sim.wallNormal, continuationNormal) > 0.95f,
                 "valid continuation contact should update the wall attachment normal");
    ok &= expect(glm::length(glm::vec3{vel.x, 0.0f, vel.z}) > 300.0f,
                 "valid continuation contact should preserve horizontal wallrun speed");
    return ok;
}

bool wallrunCeilingConstraintKeepsWallrunAfterCeilingEnds()
{
    std::array<WorldTriMesh, 2> meshes{makeThinWallSegment(-128.0f, 192.0f), makeShortThinCeiling(-48.0f, 48.0f)};
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(meshes),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-16.03125f, 4.0f, -40.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 320.0f, 520.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 16.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {16.0f, 36.0f, 16.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {0.0f, 4.0f, -40.0f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 0u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    bool sawCeiling = false;
    bool detachedAfterCeiling = false;
    bool pastCeilingEnd = false;
    float maxY = registry.get<Position>(player).value.y;

    for (int frame = 0; frame < 40; ++frame) {
        const physics::KccFrameResult kcc = runMovementAndKccTick(registry, player, world, 1.0f / 128.0f);
        const auto& stepPos = registry.get<Position>(player);
        const auto& stepVis = registry.get<PlayerVisState>(player);
        const auto& stepSim = registry.get<PlayerSimState>(player);

        sawCeiling = sawCeiling || kcc.hitCeiling || stepSim.wallrunCeilingConstrainedTimer > 0.0f;
        pastCeilingEnd = pastCeilingEnd || stepPos.value.z > 56.0f;
        maxY = std::max(maxY, stepPos.value.y);
        if (sawCeiling && pastCeilingEnd && stepVis.moveMode != MoveMode::WallRunning)
            detachedAfterCeiling = true;
    }

    const auto& outVis = registry.get<PlayerVisState>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);

    bool ok = true;
    ok &= expect(sawCeiling, "wallrun should report ceiling constraint when capsule head hits the ceiling");
    ok &= expect(pastCeilingEnd, "wallrun test should travel beyond the short ceiling segment");
    ok &=
        expect(!detachedAfterCeiling, "wallrun should not detach when a ceiling constraint ends and the wall remains");
    ok &= expect(outVis.moveMode == MoveMode::WallRunning, "wallrun should still be active after leaving the ceiling");
    ok &= expect(outSim.wallAttachmentValid, "wall attachment identity should survive ceiling constraint");
    ok &= expect(maxY <= 8.1f, "ceiling constraint should prevent the wallrun from rising through the ceiling");
    return ok;
}

bool wallrunStandoffCorrectionDoesNotMovePositionBeforeKcc()
{
    const WorldTriMesh wall = makeThinWall();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-30.0f, 20.0f, 0.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {0.0f, 20.0f, 0.0f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 1u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    const glm::vec3 before = registry.get<Position>(player).value;
    systems::runMovement(registry, 1.0f / 128.0f, world);
    const glm::vec3 after = registry.get<Position>(player).value;

    bool ok = true;
    ok &=
        expect(glm::length(after - before) < 0.01f,
               "wallrun standoff correction should be expressed through KCC-owned velocity, not direct position snap");
    ok &= expect(registry.get<PlayerVisState>(player).moveMode == MoveMode::WallRunning,
                 "valid wallrun attachment should remain active without a pre-KCC snap");
    return ok;
}

bool wallrunStandoffCorrectionIsSolvedByKccWithoutChangingVelocity()
{
    const WorldTriMesh wall = makeThinWall();
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&wall, 1),
    };

    Registry registry;
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, glm::vec3{-15.0f, 20.0f, 0.0f});
    registry.emplace<Velocity>(player, glm::vec3{0.0f, 0.0f, 500.0f});

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};
    registry.emplace<CollisionShape>(player, shape);

    PlayerVisState vis;
    vis.moveMode = MoveMode::WallRunning;
    vis.wallRunSide = WallSide::Right;
    vis.grounded = false;
    registry.emplace<PlayerVisState>(player, vis);

    PlayerSimState sim;
    sim.wallNormal = {-1.0f, 0.0f, 0.0f};
    sim.wallForward = {0.0f, 0.0f, 1.0f};
    sim.wallAnchor = {0.0f, 20.0f, 0.0f};
    sim.wallMeshIndex = 0u;
    sim.wallTriId = 1u;
    sim.wallRegion = physics::TriRegion::Face;
    sim.wallAttachmentValid = true;
    registry.emplace<PlayerSimState>(player, sim);

    InputSnapshot input;
    input.jump = true;
    input.forward = true;
    input.yaw = 0.0f;
    registry.emplace<InputSnapshot>(player, input);

    systems::runMovement(registry, 1.0f / 128.0f, world);
    const glm::vec3 afterMovementPos = registry.get<Position>(player).value;
    const glm::vec3 afterMovementVel = registry.get<Velocity>(player).value;
    const glm::vec3 wallNormal = registry.get<PlayerSimState>(player).wallNormal;

    systems::runKinematicCharacterController(registry.get<Position>(player).value,
                                             registry.get<Velocity>(player).value,
                                             registry.get<CollisionShape>(player),
                                             registry.get<PlayerVisState>(player),
                                             1.0f / 128.0f,
                                             world,
                                             player,
                                             registry.get<PlayerSimState>(player).jumpedThisTick,
                                             &registry.get<PlayerSimState>(player));

    const auto& outPos = registry.get<Position>(player);
    const auto& outVel = registry.get<Velocity>(player);
    const auto& outSim = registry.get<PlayerSimState>(player);

    bool ok = true;
    ok &= expectNear(afterMovementPos.x, -15.0f, 0.01f, "movement should not snap wallrun standoff directly");
    ok &= expect(std::abs(glm::dot(afterMovementVel, wallNormal)) < 0.01f,
                 "wallrun standoff correction should not pollute gameplay velocity before KCC");
    ok &= expectNear(glm::dot(outPos.value - outSim.wallAnchor, outSim.wallNormal),
                     10.03125f,
                     0.05f,
                     "KCC should solve wallrun standoff as swept position correction");
    ok &= expect(std::abs(glm::dot(outVel.value, outSim.wallNormal)) < 0.01f,
                 "KCC standoff solve should preserve gameplay velocity tangent to the wall");
    return ok;
}

bool kccDeepOverlapRollsBackToLastSafePosition()
{
    const physics::WorldAABB block{.min = {-64.0f, -64.0f, -64.0f}, .max = {64.0f, 64.0f, 64.0f}};
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = std::span<const physics::WorldAABB>(&block, 1),
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = {},
    };

    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    glm::vec3 vel{100.0f, 0.0f, 0.0f};

    CollisionShape shape;
    shape.type = CollisionShapeType::Capsule;
    shape.radius = 10.0f;
    shape.halfHeight = 20.0f;
    shape.halfExtents = {10.0f, 30.0f, 10.0f};

    PlayerVisState vis;
    vis.grounded = false;

    PlayerSimState sim;
    sim.lastSafePosition = {96.0f, 0.0f, 0.0f};
    sim.lastSafePositionValid = true;

    systems::runKinematicCharacterController(pos, vel, shape, vis, 1.0f / 128.0f, world, entt::null, false, &sim);

    bool ok = true;
    ok &= expectNear(pos.x, 96.0f, 0.01f, "deep player overlap should roll back to last safe X");
    ok &= expectNear(pos.y, 0.0f, 0.01f, "deep player overlap should roll back to last safe Y");
    ok &= expectNear(pos.z, 0.0f, 0.01f, "deep player overlap should roll back to last safe Z");
    ok &= expect(glm::length(vel) < 0.01f, "last-safe rollback should stop unsafe carry-through velocity");
    return ok;
}

bool triMeshValidationReportsCookerIssues()
{
    WorldTriMesh mesh;
    mesh.vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f}, // Duplicate position used to form a degenerate triangle.
    };
    mesh.indices = {
        0,
        1,
        2, // Base triangle.
        0,
        2,
        1, // Opposite winding duplicate of the base triangle.
        0,
        1,
        3, // Shares edge 0-1.
        1,
        0,
        4, // Degenerate and also a third face on edge 0-1.
    };

    const physics::TriMeshValidationReport report = physics::validateTriMesh(mesh);

    bool ok = true;
    ok &= expect(report.triangleCount == 4u, "validation should report triangle count");
    ok &= expect(report.degenerateTriangles == 1u, "validation should count degenerate triangles");
    ok &= expect(report.duplicatedOppositeWindingFaces == 1u, "validation should count opposite-winding duplicates");
    ok &= expect(report.nonManifoldEdges == 1u, "validation should count non-manifold edges");
    ok &= expect(!report.valid(), "validation report should mark problematic meshes invalid");
    return ok;
}

bool triMeshValidationTotalsAggregateAuthoredSet()
{
    WorldTriMesh badMesh;
    badMesh.vertices = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
    };
    badMesh.indices = {
        0,
        1,
        2,
        0,
        2,
        1,
        0,
        1,
        3,
    };
    std::array<WorldTriMesh, 2> meshes{makeTwoTriangleFloor(), badMesh};

    const physics::TriMeshValidationTotals totals = physics::validateTriMeshes(std::span<const WorldTriMesh>(meshes));

    bool ok = true;
    ok &= expect(totals.meshCount == 2u, "validation totals should report mesh count");
    ok &= expect(totals.invalidMeshCount == 1u, "validation totals should count invalid meshes");
    ok &= expect(totals.triangleCount == 5u, "validation totals should aggregate triangle count");
    ok &= expect(totals.degenerateTriangles == 1u, "validation totals should aggregate degenerates");
    ok &= expect(totals.duplicatedOppositeWindingFaces == 1u,
                 "validation totals should aggregate opposite-winding duplicates");
    ok &= expect(!totals.valid(), "validation totals should mark problematic authored sets invalid");
    return ok;
}

bool triMeshCookStatsReportWeldAndBvhQuality()
{
    const WorldTriMesh floor = makeTwoTriangleFloor();
    const physics::TriMeshCookStats stats = physics::collectTriMeshCookStats(std::span<const WorldTriMesh>(&floor, 1));

    bool ok = true;
    ok &= expect(stats.meshCount == 1u, "cook stats should report mesh count");
    ok &= expect(stats.vertexCount == 4u, "cook stats should report vertex count");
    ok &= expect(stats.triangleCount == 2u, "cook stats should report triangle count");
    ok &= expect(stats.meshBvhNodeCount == 1u, "cook stats should report mesh BVH nodes");
    ok &= expect(stats.meshBvhLeafCount == 1u, "cook stats should report mesh BVH leaves");
    ok &= expect(stats.maxMeshBvhDepth == 1u, "cook stats should report mesh BVH depth");
    ok &= expect(stats.activeHalfEdges == 4u, "cook stats should keep boundary half-edges active");
    ok &= expect(stats.boundaryHalfEdges == 4u, "cook stats should report boundary half-edges");
    ok &= expect(stats.weldedHalfEdges == 2u, "cook stats should report welded planar seam half-edges");
    ok &= expect(stats.invalidNormals == 0u, "cook stats should report invalid cooked normals");
    return ok;
}

bool duplicatedSeamVerticesStillWeldCoplanarFloor()
{
    const WorldTriMesh floor = makeTwoTriangleFloorWithDuplicatedSeamVertices();
    const physics::TriMeshCookStats stats = physics::collectTriMeshCookStats(std::span<const WorldTriMesh>(&floor, 1));

    bool ok = true;
    ok &= expect(stats.activeHalfEdges == 4u, "duplicated-position floor seam should not become active boundary edges");
    ok &= expect(stats.boundaryHalfEdges == 4u, "duplicated-position floor should keep only true outer boundaries");
    ok &= expect(stats.weldedHalfEdges == 2u, "duplicated-position floor seam should weld both internal half-edges");
    ok &= expect(floor.edgeNeighbor.size() == 6u, "duplicated-position floor should have one neighbour slot per edge");
    ok &= expect(floor.edgeNeighbor[0u] == 1u, "duplicated-position floor first triangle should link across seam");
    ok &=
        expect(floor.edgeNeighbor[5u] == 0u, "duplicated-position floor second triangle should link back across seam");
    return ok;
}

struct PlayerSnapshot
{
    glm::vec3 pos{0.0f};
    glm::vec3 vel{0.0f};
    CollisionShape shape{};
    PlayerVisState vis{};
    PlayerSimState sim{};
};

entt::entity createReplayPlayer(Registry& registry, const PlayerSnapshot& snap)
{
    const entt::entity player = registry.create();
    registry.emplace<Position>(player, snap.pos);
    registry.emplace<Velocity>(player, snap.vel);
    registry.emplace<CollisionShape>(player, snap.shape);
    registry.emplace<PlayerVisState>(player, snap.vis);
    registry.emplace<PlayerSimState>(player, snap.sim);
    registry.emplace<InputSnapshot>(player);
    return player;
}

PlayerSnapshot snapshotPlayer(const Registry& registry, entt::entity player)
{
    return {
        .pos = registry.get<Position>(player).value,
        .vel = registry.get<Velocity>(player).value,
        .shape = registry.get<CollisionShape>(player),
        .vis = registry.get<PlayerVisState>(player),
        .sim = registry.get<PlayerSimState>(player),
    };
}

void simulateReplayTick(Registry& registry,
                        entt::entity player,
                        const physics::WorldGeometry& world,
                        const InputSnapshot& input)
{
    registry.replace<InputSnapshot>(player, input);
    systems::runMovement(registry, 1.0f / 128.0f, world);
    systems::runKinematicCharacterController(registry.get<Position>(player).value,
                                             registry.get<Velocity>(player).value,
                                             registry.get<CollisionShape>(player),
                                             registry.get<PlayerVisState>(player),
                                             1.0f / 128.0f,
                                             world,
                                             player,
                                             registry.get<PlayerSimState>(player).jumpedThisTick,
                                             &registry.get<PlayerSimState>(player));
}

bool predictionReplayMatchesDirectKccOnThinStairs()
{
    const WorldTriMesh stairs = makeThinStaircase(12, 12.0f, 28.0f);
    const physics::WorldGeometry world{
        .planes = {},
        .boxes = {},
        .brushes = {},
        .cylinders = {},
        .spheres = {},
        .triMeshes = std::span<const WorldTriMesh>(&stairs, 1),
    };

    PlayerSnapshot initial;
    initial.pos = {0.0f, 30.03125f, -40.0f};
    initial.shape.type = CollisionShapeType::Capsule;
    initial.shape.radius = 10.0f;
    initial.shape.halfHeight = 20.0f;
    initial.shape.halfExtents = {10.0f, 30.0f, 10.0f};
    initial.vis.grounded = true;
    initial.vis.groundNormal = {0.0f, 1.0f, 0.0f};

    constexpr int k_tickCount = 96;
    constexpr int k_ackedTick = 47;
    std::array<InputSnapshot, k_tickCount> inputs{};
    for (int i = 0; i < k_tickCount; ++i) {
        inputs[static_cast<size_t>(i)].forward = true;
        inputs[static_cast<size_t>(i)].yaw = 0.0f;
        inputs[static_cast<size_t>(i)].jump = i >= 72 && i < 76;
    }

    Registry directRegistry;
    const entt::entity directPlayer = createReplayPlayer(directRegistry, initial);
    PlayerSnapshot ackSnapshot{};
    for (int tick = 0; tick < k_tickCount; ++tick) {
        simulateReplayTick(directRegistry, directPlayer, world, inputs[static_cast<size_t>(tick)]);
        if (tick == k_ackedTick)
            ackSnapshot = snapshotPlayer(directRegistry, directPlayer);
    }
    const PlayerSnapshot directFinal = snapshotPlayer(directRegistry, directPlayer);

    Registry replayRegistry;
    const entt::entity replayPlayer = createReplayPlayer(replayRegistry, ackSnapshot);
    for (int tick = k_ackedTick + 1; tick < k_tickCount; ++tick)
        simulateReplayTick(replayRegistry, replayPlayer, world, inputs[static_cast<size_t>(tick)]);
    const PlayerSnapshot replayFinal = snapshotPlayer(replayRegistry, replayPlayer);

    bool ok = true;
    ok &= expectNear(replayFinal.pos.x, directFinal.pos.x, 0.001f, "prediction replay should match direct X");
    ok &= expectNear(replayFinal.pos.y, directFinal.pos.y, 0.001f, "prediction replay should match direct Y");
    ok &= expectNear(replayFinal.pos.z, directFinal.pos.z, 0.001f, "prediction replay should match direct Z");
    ok &= expectNear(replayFinal.vel.x, directFinal.vel.x, 0.001f, "prediction replay should match direct VX");
    ok &= expectNear(replayFinal.vel.y, directFinal.vel.y, 0.001f, "prediction replay should match direct VY");
    ok &= expectNear(replayFinal.vel.z, directFinal.vel.z, 0.001f, "prediction replay should match direct VZ");
    ok &= expect(replayFinal.vis.grounded == directFinal.vis.grounded, "prediction replay should match grounded state");
    ok &= expect(replayFinal.vis.moveMode == directFinal.vis.moveMode, "prediction replay should match move mode");
    ok &= expectNear(replayFinal.vis.groundNormal.x,
                     directFinal.vis.groundNormal.x,
                     0.001f,
                     "prediction replay should match ground normal X");
    ok &= expectNear(replayFinal.vis.groundNormal.y,
                     directFinal.vis.groundNormal.y,
                     0.001f,
                     "prediction replay should match ground normal Y");
    ok &= expectNear(replayFinal.vis.groundNormal.z,
                     directFinal.vis.groundNormal.z,
                     0.001f,
                     "prediction replay should match ground normal Z");
    return ok;
}

bool playerWallAttachmentStateHasStableMeshIdentity()
{
    PlayerSimState state;
    bool ok = true;
    ok &= expect(state.wallMeshIndex == UINT32_MAX, "wall attachment should default to no mesh identity");
    ok &= expect(state.wallTriId == UINT32_MAX, "wall attachment should default to no triangle identity");
    ok &= expect(!state.wallAttachmentValid, "wall attachment should default to invalid");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok &= thinWallPlaneBlocksFromBothSides();
    ok &= simultaneousCornerSweepTieBreakIsMeshOrderIndependent();
    ok &= sweptCapsuleHitsFiniteWallEdge();
    ok &= walkCapsuleStepsOntoThinTrimeshTread();
    ok &= groundProbeIgnoresDegenerateSnapCandidate();
    ok &= kccStepsOntoThinTrimeshStep();
    ok &= kccAscendsThinTrimeshStaircaseSmoothly();
    ok &= kccAscendsThinTrimeshStaircaseAtSprintSpeed();
    ok &= thinCeilingPlaneBlocksUpwardCapsuleMotion();
    ok &= airborneKccDoesNotSnapToSlopedCeiling();
    ok &= airborneKccDoesNotGainLiftFromSeparatingSlopedCeiling();
    ok &= rampGroundProbeKeepsAuthoredNormal();
    ok &= staticCapsuleDepenUsesSurfaceFeatureNormal();
    ok &= twoTriangleFloorHasStableSurfaceOverlap();
    ok &= sphereCastAgainstTriMeshUsesSurfaceFeatureNormal();
    ok &= staticBroadphaseReturnsOnlyOverlappingTriMeshes();
    ok &= wallDetectionTracksOverlappingThinTriMeshWall();
    ok &= wallDetectionGroundDistanceUsesFlippedGravityDirection();
    ok &= wallAttachmentLookaheadFindsOuterCornerContinuation();
    ok &= wallAttachmentWalksSingleMeshAdjacencyAtCorner();
    ok &= wallrunOuterCornerKeepsForwardVelocity();
    ok &= wallrunSingleMeshExternalLCornerContinuesAroundCorner();
    ok &= wallrunGapDropsWithoutReversingVelocity();
    ok &= wallrunMapCorridorTurnsIntoCorridor();
    ok &= wallrunExternalCornerDefersRedirectUntilOldWallClear();
    ok &= wallrunMapExternalLCornerContinuesWithKcc();
    ok &= wallrunInternalCornerKeepsMoving();
    ok &= wallrunCapturedBalconyCornerDoesNotDropIntoJitter();
    ok &= wallrunBlockedCornerTransitionDoesNotTimeoutIntoCooldownLoop();
    ok &= wallrunRequiresMinimumAttachSpeed();
    ok &= wallrunDropsBelowMinimumStaySpeedWithoutCooldown();
    ok &= wallrunCanAttachToDifferentWallImmediatelyAfterExit();
    ok &= wallrunSameWallBlacklistIsSpeedBased();
    ok &= wallrunRollScalesWithViewAngle();
    ok &= wallrunMapCorridorEndDoesNotAttachToAirSide();
    ok &= wallrunSolidEndcapSlidesAsBlockerWithoutWrongWallHandoff();
    ok &= wallrunDisconnectedOctagonFrontFaceContinuesInsteadOfDropping();
    ok &= wallrunCapturedMapOctagonTransitionDoesNotDropAcrossKcc();
    ok &= wallrunKccContactOnContinuationFaceDoesNotBecomeBlocker();
    ok &= wallrunCeilingConstraintKeepsWallrunAfterCeilingEnds();
    ok &= wallrunStandoffCorrectionDoesNotMovePositionBeforeKcc();
    ok &= wallrunStandoffCorrectionIsSolvedByKccWithoutChangingVelocity();
    ok &= kccDeepOverlapRollsBackToLastSafePosition();
    ok &= triMeshValidationReportsCookerIssues();
    ok &= triMeshValidationTotalsAggregateAuthoredSet();
    ok &= triMeshCookStatsReportWeldAndBvhQuality();
    ok &= duplicatedSeamVerticesStillWeldCoplanarFloor();
    ok &= predictionReplayMatchesDirectKccOnThinStairs();
    ok &= playerWallAttachmentStateHasStableMeshIdentity();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
