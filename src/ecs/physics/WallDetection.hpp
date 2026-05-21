/// @file WallDetection.hpp
/// @brief Wall detection against static world collision.

#pragma once

#include "SweptCollision.hpp"
#include "TriMeshCollision.hpp"

#include <cmath>
#include <cstdint>
#include <glm/vec3.hpp>

/// @brief Wall detection.
///
/// Used by the movement system each tick to detect nearby surfaces
/// for wallrunning.
namespace physics
{

/// @brief Results of wall detection probes.
struct WallDetectionResult
{
    // Side walls (wallrunning)
    bool wallLeft{false};                   ///< True if a wall was detected to the left.
    bool wallRight{false};                  ///< True if a wall was detected to the right.
    glm::vec3 leftNormal{0.0f};             ///< Surface normal of the left wall.
    glm::vec3 rightNormal{0.0f};            ///< Surface normal of the right wall.
    glm::vec3 leftPoint{0.0f};              ///< World-space contact point on the left wall.
    glm::vec3 rightPoint{0.0f};             ///< World-space contact point on the right wall.
    uint32_t leftMeshIndex{UINT32_MAX};     ///< Static trimesh index for stable wallrun attachment, if applicable.
    uint32_t rightMeshIndex{UINT32_MAX};    ///< Static trimesh index for stable wallrun attachment, if applicable.
    uint32_t leftTriId{UINT32_MAX};         ///< Triangle id for stable wallrun attachment, if applicable.
    uint32_t rightTriId{UINT32_MAX};        ///< Triangle id for stable wallrun attachment, if applicable.
    TriRegion leftRegion{TriRegion::Face};  ///< Closest triangle feature for wallrun seam traversal.
    TriRegion rightRegion{TriRegion::Face}; ///< Closest triangle feature for wallrun seam traversal.

    // Front wall diagnostics.
    bool wallFront{false};                  ///< True if a wall was detected in front.
    glm::vec3 frontNormal{0.0f};            ///< Surface normal of the front wall.
    glm::vec3 frontPoint{0.0f};             ///< World-space contact point on the front wall.
    uint32_t frontMeshIndex{UINT32_MAX};    ///< Static trimesh index for the front-wall diagnostic, if applicable.
    uint32_t frontTriId{UINT32_MAX};        ///< Triangle id for the front-wall diagnostic, if applicable.
    TriRegion frontRegion{TriRegion::Face}; ///< Closest triangle feature for the front-wall diagnostic.

    // Ground distance
    float groundDistance{1e10f}; ///< Distance to ground below the player (u).
};

/// @brief Stable wallrun attachment target on authored triangle meshes.
struct WallAttachmentResult
{
    bool found{false};
    glm::vec3 anchor{0.0f};
    glm::vec3 normal{0.0f};
    uint32_t meshIndex{UINT32_MAX};
    uint32_t triId{UINT32_MAX};
    TriRegion region{TriRegion::Face};
};

/// @brief Detect walls to the left, right, and front of the player.
///
/// Also probes downward to measure ground distance (used for wallrun min-height gates).
///
/// @param pos            Player AABB centre position.
/// @param yaw            Player facing direction (radians).
/// @param halfExtents    Player AABB half-extents (for offset calculations).
/// @param world          World collision geometry.
/// @param checkDist      How far sideways/forward to trace (u).
/// @param sphereRadius   Radius of the trace sphere (u).
/// @param prevWallNormal Previous tick's wall normal (zero if not wallrunning).
///                       When non-zero, an additional trace is cast toward
///                       `-prevWallNormal` to track curved surfaces (cylinders,
///                       concave walls) whose normal rotates as the player moves.
/// @param gravityFlipped True when the player's local up axis is -Y.
/// @return               Detection results for all directions.
WallDetectionResult detectWalls(glm::vec3 pos,
                                float yaw,
                                glm::vec3 halfExtents,
                                const WorldGeometry& world,
                                float checkDist,
                                float sphereRadius,
                                glm::vec3 prevWallNormal = glm::vec3(0.0f),
                                bool gravityFlipped = false);

/// @brief Find the best triangle-mesh wallrun attachment, with optional
/// lookahead along the current travel direction.
///
/// Current-position attachment keeps ordinary wallruns stable. The lookahead
/// sample lets convex/outside corners select the upcoming perpendicular wall
/// before the old wall's closest point flips the tangent backward.
WallAttachmentResult findWallRunAttachment(CapsuleShape capsule,
                                           glm::vec3 pos,
                                           const WorldGeometry& world,
                                           glm::vec3 continuityNormal,
                                           glm::vec3 travelDir = glm::vec3{0.0f},
                                           float lookaheadDist = 0.0f,
                                           float checkDist = 24.0f,
                                           uint32_t previousMeshIndex = UINT32_MAX,
                                           uint32_t previousTriId = UINT32_MAX,
                                           TriRegion previousRegion = TriRegion::Face);

/// @brief Check if a surface normal represents a wall (not floor/ceiling).
///
/// Walls have normals that are roughly horizontal (|normal.y| < 0.3).
inline bool isWallNormal(glm::vec3 normal)
{
    return std::abs(normal.y) < 0.3f;
}

} // namespace physics
