/// @file WorldData.hpp
/// @brief Shared test world geometry compiled identically on client and server.

#pragma once

/// @brief Shared test world geometry — compiled identically on client and server.
///
/// Defines the physics playground: floor, boxes, ramps, walls, pole, walkway.
/// Both client and server must use the same geometry for prediction parity.

#include "SweptCollision.hpp"

#include <array>
#include <cmath>
#include <glm/geometric.hpp>

namespace physics
{

// Helper factories

/// @brief Create a ramp brush that rises along +Z.
///
/// The wedge shape goes from (xMin, 0, zMin) at ground level to
/// (xMax, height, zMax) at the back-top edge.
inline WorldBrush makeRamp(float xMin, float xMax, float zMin, float zMax, float height)
{
    WorldBrush b;

    const float k_depth = zMax - zMin;
    const float k_len = std::sqrt(k_depth * k_depth + height * height);
    const glm::vec3 k_slopeNorm{0.0f, k_depth / k_len, -height / k_len};

    // Slope surface distance: dot(normal, point_on_slope).
    // The front-bottom corner (0, 0, zMin) sits on the slope.
    const float k_slopeDist = glm::dot(k_slopeNorm, glm::vec3(0.0f, 0.0f, zMin));

    b.planes[0] = {k_slopeNorm, k_slopeDist}; // slope (walkable surface)
    b.planes[1] = {{0, -1, 0}, 0.0f};         // bottom
    b.planes[2] = {{-1, 0, 0}, -xMin};        // left side
    b.planes[3] = {{1, 0, 0}, xMax};          // right side
    b.planes[4] = {{0, 0, -1}, -zMin};        // front face
    b.planes[5] = {{0, 0, 1}, zMax};          // back face
    b.planeCount = 6;

    return b;
}

/// @brief Create a diagonal wall brush from a centre, direction, and dimensions.
///
/// @param center       Centre of the wall base (y=0).
/// @param halfLen      Half-length along @p dir.
/// @param halfThick    Half-thickness perpendicular to @p dir in XZ.
/// @param height       Wall height (from y=0 to y=height).
/// @param dir          Normalised direction along the wall in XZ.
inline WorldBrush makeDiagonalWall(glm::vec3 center, float halfLen, float halfThick, float height, glm::vec3 dir)
{
    WorldBrush b;

    // Face normal: 90-degree rotation of dir in the XZ plane.
    const glm::vec3 k_faceN{dir.z, 0.0f, -dir.x};

    b.planes[0] = {k_faceN, glm::dot(k_faceN, center + k_faceN * halfThick)};   // front face
    b.planes[1] = {-k_faceN, glm::dot(-k_faceN, center - k_faceN * halfThick)}; // back face
    b.planes[2] = {dir, glm::dot(dir, center + dir * halfLen)};                 // right end
    b.planes[3] = {-dir, glm::dot(-dir, center - dir * halfLen)};               // left end
    b.planes[4] = {{0, 1, 0}, height};                                          // top
    b.planes[5] = {{0, -1, 0}, 0.0f};                                           // bottom
    b.planeCount = 6;

    return b;
}

// Test world

/// @brief The physics test playground.
///
/// Layout along the +Z axis (forward from spawn at origin):
///
///   z ~ 400  : Reference cube (64^3)
///   z ~ 800  : Small steppable box (64x16x64) and large jumpable box (80x64x80)
///   z ~ 1000 : Gentle ramp (15deg, left) and steep ramp (40deg, right)
///   z ~ 1500 : Stairs (5 steps rising along +Z)
///   z ~ 1900 : Axis-aligned wall, diagonal wall, pole
///   z ~ 2100 : Elevated thin walkway
inline const WorldGeometry& testWorld()
{
    // Infinite planes
    static const std::array<Plane, 1> k_planes = {{
        {.normal = {0, 1, 0}, .distance = 0.0f}, // floor at y=0
    }};

    // Axis-aligned boxes
    static const std::array<WorldAABB, 30> k_boxes = {{
        // 0. Reference cube (existing visual — now also collidable)
        {{-32, 0, 368}, {32, 64, 432}},

        // 1. Small steppable box (16 units tall < stepHeight=18)
        {{68, 0, 768}, {132, 16, 832}},

        // 2. Large jumpable box (64 units tall — requires a jump)
        {{-140, 0, 760}, {-60, 64, 840}},

        // 3-7. Stairs — 5 steps, each 16u high × 48u deep, 128u wide
        {{-64, 0, 1500}, {64, 16, 1548}},
        {{-64, 0, 1548}, {64, 32, 1596}},
        {{-64, 0, 1596}, {64, 48, 1644}},
        {{-64, 0, 1644}, {64, 64, 1692}},
        {{-64, 0, 1692}, {64, 80, 1740}},

        // 8. Axis-aligned wall (256 wide × 120 tall × 16 thick)
        {{-128, 0, 1892}, {128, 120, 1908}},

        // 9. Pole (16×16 base × 200 tall)
        {{-258, 0, 1892}, {-242, 200, 1908}},

        // 10. Thin elevated walkway (32 wide × 16 tall × 400 long, at y=80)
        {{-16, 80, 2100}, {16, 96, 2500}},

        // Wallrun corridor (parallel walls, 400u long, 200u tall, 200u apart)
        // 11. Left wallrun wall
        {{-116, 0, 2700}, {-100, 200, 3100}},
        // 12. Right wallrun wall
        {{100, 0, 2700}, {116, 200, 3100}},

        // Wall-to-wall jump section (offset walls for chaining wallruns)
        // 13. Left wall (offset)
        {{-116, 0, 3300}, {-100, 200, 3600}},
        // 14. Right wall (offset further)
        {{140, 0, 3400}, {156, 200, 3700}},

        // Climb wall (tall flat wall, 300u tall)
        // 15. Climb wall (front-facing from the corridor)
        {{-64, 0, 3900}, {64, 300, 3916}},

        // Ledge wall (medium height wall, 120u -- reachable via climb)
        // 16. Ledge wall with flat top
        {{200, 0, 3900}, {328, 120, 3916}},

        // Slide run (long flat stretch for sprint -> slide)
        // The floor itself is the slide surface, but add side walls to guide
        // 17. Left guide wall
        {{-200, 0, 4100}, {-184, 40, 4600}},
        // 18. Right guide wall
        {{184, 0, 4100}, {200, 40, 4600}},

        // Combined parkour course
        // 19. Platform (jump up to start the course)
        {{-48, 0, 4800}, {48, 48, 4848}},
        // 20. Left wallrun wall (angled course)
        {{-140, 0, 4900}, {-124, 200, 5300}},
        // 21. Right wallrun wall (angled course)
        {{124, 0, 5000}, {140, 200, 5400}},
        // 22. Climb target (at end of wallrun)
        {{-64, 0, 5500}, {64, 250, 5516}},
        // 23. Ledge platform (on top of a medium wall)
        {{200, 0, 5500}, {328, 100, 5516}},
        // 24. Landing pad (beyond the climb wall)
        {{-80, 0, 5550}, {80, 16, 5650}},

        // Grapple test: arch + high platform
        // 25. Arch left pillar (32×500×32, very tall)
        {{-116, 0, 6000}, {-84, 500, 6032}},
        // 26. Arch right pillar
        {{84, 0, 6000}, {116, 500, 6032}},
        // 27. Arch crossbar (connects pillars at y=460, 232 wide × 40 tall × 32 deep)
        {{-116, 460, 6000}, {116, 500, 6032}},
        // 28. High platform (behind the arch, at y=400, 200×16×100)
        {{-100, 400, 6200}, {100, 416, 6300}},
        // 29. Ultra-high target platform (y=600, small, way up there)
        {{-48, 580, 6500}, {48, 596, 6548}},
    }};

    // Convex brushes
    static const std::array<WorldBrush, 3> k_brushes = {{
        // Gentle ramp (15 deg): x ∈ [-214, -86], z ∈ [950, 1250], rises to 80u
        makeRamp(-214.0f, -86.0f, 950.0f, 1250.0f, 80.0f),

        // Steep ramp (40 deg): x ∈ [86, 214], z ∈ [1000, 1200], rises to 168u
        makeRamp(86.0f, 214.0f, 1000.0f, 1200.0f, 168.0f),

        // Diagonal wall (45 deg): centre (300, 0, 1900), length 200, thick 16, height 120
        makeDiagonalWall({300.0f, 0.0f, 1900.0f}, 100.0f, 8.0f, 120.0f, glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f))),
    }};

    static const WorldGeometry k_geo{k_planes, k_boxes, k_brushes};
    return k_geo;
}

} // namespace physics
