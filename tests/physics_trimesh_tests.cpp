#include "ecs/components/PlayerSimState.hpp"
#include "ecs/physics/TriMeshCollision.hpp"
#include "ecs/physics/WallDetection.hpp"
#include "ecs/systems/KinematicCharacterController.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <entt/entity/entity.hpp>
#include <iostream>
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

bool kccClimbsThinTrimeshStep()
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
    ok &= sweptCapsuleHitsFiniteWallEdge();
    ok &= walkCapsuleStepsOntoThinTrimeshTread();
    ok &= kccClimbsThinTrimeshStep();
    ok &= thinCeilingPlaneBlocksUpwardCapsuleMotion();
    ok &= rampGroundProbeKeepsAuthoredNormal();
    ok &= staticCapsuleDepenUsesSurfaceFeatureNormal();
    ok &= twoTriangleFloorHasStableSurfaceOverlap();
    ok &= sphereCastAgainstTriMeshUsesSurfaceFeatureNormal();
    ok &= staticBroadphaseReturnsOnlyOverlappingTriMeshes();
    ok &= wallDetectionTracksOverlappingThinTriMeshWall();
    ok &= triMeshValidationReportsCookerIssues();
    ok &= triMeshCookStatsReportWeldAndBvhQuality();
    ok &= playerWallAttachmentStateHasStableMeshIdentity();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
