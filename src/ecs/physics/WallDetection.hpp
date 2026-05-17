/// @file WallDetection.hpp
/// @brief Wall, climb, and ledge detection against static world collision.

#pragma once

#include "SweptCollision.hpp"
#include "TriMeshCollision.hpp"

#include <cmath>
#include <cstdint>
#include <glm/vec3.hpp>

/// @brief Wall / climb / ledge detection.
///
/// Used by the movement system each tick to detect nearby surfaces
/// for wallrunning, climbing, and ledge grabbing.
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

    // Front wall (climbing)
    bool wallFront{false};                  ///< True if a wall was detected in front.
    glm::vec3 frontNormal{0.0f};            ///< Surface normal of the front wall.
    glm::vec3 frontPoint{0.0f};             ///< World-space contact point on the front wall.
    uint32_t frontMeshIndex{UINT32_MAX};    ///< Static trimesh index for stable climb attachment, if applicable.
    uint32_t frontTriId{UINT32_MAX};        ///< Triangle id for stable climb attachment, if applicable.
    TriRegion frontRegion{TriRegion::Face}; ///< Closest triangle feature for climb seam traversal.

    // Ledge (top of front wall)
    bool ledgeDetected{false};   ///< True if a ledge was detected above the front wall.
    glm::vec3 ledgePoint{0.0f};  ///< World-space point on the ledge surface.
    glm::vec3 ledgeNormal{0.0f}; ///< Wall normal at the ledge.

    // Ground distance
    float groundDistance{1e10f}; ///< Distance to ground below the player (u).
};

/// @brief Detect walls to the left, right, and front of the player.
///
/// Also probes downward to measure ground distance (used for wallrun/climb min height).
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
/// @return               Detection results for all directions.
WallDetectionResult detectWalls(glm::vec3 pos,
                                float yaw,
                                glm::vec3 halfExtents,
                                const WorldGeometry& world,
                                float checkDist,
                                float sphereRadius,
                                glm::vec3 prevWallNormal = glm::vec3(0.0f));

/// @brief Check if a surface normal represents a wall (not floor/ceiling).
///
/// Walls have normals that are roughly horizontal (|normal.y| < 0.3).
inline bool isWallNormal(glm::vec3 normal)
{
    return std::abs(normal.y) < 0.3f;
}

} // namespace physics
