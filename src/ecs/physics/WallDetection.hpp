/// @file WallDetection.hpp
/// @brief Wall, climb, and ledge detection via sphere casts.

#pragma once

#include "SweptCollision.hpp"

#include <cmath>
#include <glm/vec3.hpp>

/// @brief Wall / climb / ledge detection via sphere casts.
///
/// Used by the movement system each tick to detect nearby surfaces
/// for wallrunning, climbing, and ledge grabbing.
namespace physics
{

/// @brief Results of wall detection sphere casts.
struct WallDetectionResult
{
    // Side walls (wallrunning)
    bool wallLeft{false};        ///< True if a wall was detected to the left.
    bool wallRight{false};       ///< True if a wall was detected to the right.
    glm::vec3 leftNormal{0.0f};  ///< Surface normal of the left wall.
    glm::vec3 rightNormal{0.0f}; ///< Surface normal of the right wall.
    glm::vec3 leftPoint{0.0f};   ///< World-space contact point on the left wall.
    glm::vec3 rightPoint{0.0f};  ///< World-space contact point on the right wall.

    // Front wall (climbing)
    bool wallFront{false};       ///< True if a wall was detected in front.
    glm::vec3 frontNormal{0.0f}; ///< Surface normal of the front wall.
    glm::vec3 frontPoint{0.0f};  ///< World-space contact point on the front wall.

    // Ledge (top of front wall)
    bool ledgeDetected{false};   ///< True if a ledge was detected above the front wall.
    glm::vec3 ledgePoint{0.0f};  ///< World-space point on the ledge surface.
    glm::vec3 ledgeNormal{0.0f}; ///< Wall normal at the ledge.

    // Ground distance
    float groundDistance{1e10f}; ///< Distance to ground below the player (u).
};

/// @brief Detect walls to the left, right, and front of the player via sphere casts.
///
/// Also probes downward to measure ground distance (used for wallrun/climb min height).
///
/// @param pos          Player AABB centre position.
/// @param yaw          Player facing direction (radians).
/// @param halfExtents  Player AABB half-extents (for offset calculations).
/// @param world        World collision geometry.
/// @param checkDist    How far sideways/forward to trace (u).
/// @param sphereRadius Radius of the trace sphere (u).
/// @return             Detection results for all directions.
WallDetectionResult detectWalls(
    glm::vec3 pos, float yaw, glm::vec3 halfExtents, const WorldGeometry& world, float checkDist, float sphereRadius);

/// @brief Check if a surface normal represents a wall (not floor/ceiling).
///
/// Walls have normals that are roughly horizontal (|normal.y| < 0.3).
inline bool isWallNormal(glm::vec3 normal)
{
    return std::abs(normal.y) < 0.3f;
}

} // namespace physics
