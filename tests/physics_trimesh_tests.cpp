#include "ecs/physics/TriMeshCollision.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{

using physics::CapsuleShape;
using physics::DepenContact;
using physics::HitResult;
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

} // namespace

int main()
{
    bool ok = true;
    ok &= sweptCapsuleHitsFiniteWallEdge();
    ok &= staticCapsuleDepenUsesSurfaceFeatureNormal();
    ok &= twoTriangleFloorHasStableSurfaceOverlap();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
