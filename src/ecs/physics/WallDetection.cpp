/// @file WallDetection.cpp
/// @brief Implementation of wall, climb, and ledge detection via sphere casts.

#include "WallDetection.hpp"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace physics
{

WallDetectionResult detectWalls(glm::vec3 pos,
                                float yaw,
                                glm::vec3 halfExtents,
                                const WorldGeometry& world,
                                float checkDist,
                                float sphereRadius,
                                glm::vec3 prevWallNormal)
{
    WallDetectionResult result;

    // Player's local axes in world space.
    const float k_sinYaw = std::sin(yaw);
    const float k_cosYaw = std::cos(yaw);
    const glm::vec3 k_forward{k_sinYaw, 0.0f, k_cosYaw};
    const glm::vec3 k_right{k_cosYaw, 0.0f, -k_sinYaw};

    // Side wall detection (for wallrunning)
    // Trace from the player's centre sideways, at roughly hip height.
    {
        const glm::vec3 k_rightEnd = pos + k_right * checkDist;
        const SphereHitResult k_hr = sphereCast(sphereRadius, pos, k_rightEnd, world);
        if (k_hr.hit && isWallNormal(k_hr.normal)) {
            result.wallRight = true;
            result.rightNormal = k_hr.normal;
            result.rightPoint = k_hr.point;
        }
    }
    {
        const glm::vec3 k_leftEnd = pos - k_right * checkDist;
        const SphereHitResult k_hr = sphereCast(sphereRadius, pos, k_leftEnd, world);
        if (k_hr.hit && isWallNormal(k_hr.normal)) {
            result.wallLeft = true;
            result.leftNormal = k_hr.normal;
            result.leftPoint = k_hr.point;
        }
    }

    // Curved surface tracking: when we have a previous wall normal (active
    // wallrun), cast toward the wall using -prevWallNormal.  This catches
    // curved surfaces (cylinders, concave walls) where the standard left/right
    // trace might overshoot because the surface normal has rotated.
    if (glm::length(prevWallNormal) > 0.5f) {
        const glm::vec3 k_towardWall = -prevWallNormal;
        const glm::vec3 k_traceEnd = pos + k_towardWall * checkDist;
        const SphereHitResult k_hr = sphereCast(sphereRadius, pos, k_traceEnd, world);

        if (k_hr.hit && isWallNormal(k_hr.normal)) {
            // Determine which side of the player this wall is on by checking
            // the dot product of the hit normal with the player's right axis.
            const float k_side = glm::dot(k_hr.normal, k_right);

            if (k_side < -0.1f) {
                // Wall normal points opposite to player right → wall is to the right
                if (!result.wallRight || k_hr.t < 0.5f) {
                    result.wallRight = true;
                    result.rightNormal = k_hr.normal;
                    result.rightPoint = k_hr.point;
                }
            } else if (k_side > 0.1f) {
                // Wall normal points along player right → wall is to the left
                if (!result.wallLeft || k_hr.t < 0.5f) {
                    result.wallLeft = true;
                    result.leftNormal = k_hr.normal;
                    result.leftPoint = k_hr.point;
                }
            } else {
                // Wall is roughly in front/behind — assign to whichever side
                // matches the previous wall normal more closely.
                const float k_rightDot = glm::dot(prevWallNormal, result.rightNormal);
                const float k_leftDot = glm::dot(prevWallNormal, result.leftNormal);
                if (k_rightDot > k_leftDot) {
                    result.wallRight = true;
                    result.rightNormal = k_hr.normal;
                    result.rightPoint = k_hr.point;
                } else {
                    result.wallLeft = true;
                    result.leftNormal = k_hr.normal;
                    result.leftPoint = k_hr.point;
                }
            }
        }
    }

    // Front wall detection (for climbing)
    // Trace forward from the player's centre.
    {
        const glm::vec3 k_frontEnd = pos + k_forward * checkDist;
        const SphereHitResult k_hr = sphereCast(sphereRadius, pos, k_frontEnd, world);
        if (k_hr.hit && isWallNormal(k_hr.normal)) {
            result.wallFront = true;
            result.frontNormal = k_hr.normal;
            result.frontPoint = k_hr.point;
        }
    }

    // Ledge detection
    if (result.wallFront) {
        const glm::vec3 k_headTop = pos + glm::vec3(0.0f, halfExtents.y + 10.0f, 0.0f);
        const glm::vec3 k_headTopFwd = k_headTop + k_forward * checkDist;
        const SphereHitResult k_topFwd = sphereCast(sphereRadius, k_headTop, k_headTopFwd, world);

        if (!k_topFwd.hit) {
            const glm::vec3 k_probeStart = k_headTopFwd;
            const glm::vec3 k_probeEnd = k_probeStart - glm::vec3(0.0f, halfExtents.y * 2.0f + 40.0f, 0.0f);
            const SphereHitResult k_downHit = sphereCast(sphereRadius * 0.5f, k_probeStart, k_probeEnd, world);

            if (k_downHit.hit && k_downHit.normal.y > 0.7f) {
                result.ledgeDetected = true;
                result.ledgePoint = k_downHit.point;
                result.ledgeNormal = result.frontNormal;
            }
        }
    }

    // Ground distance probe
    {
        const glm::vec3 k_feetPos = pos - glm::vec3(0.0f, halfExtents.y, 0.0f);
        const glm::vec3 k_downEnd = k_feetPos - glm::vec3(0.0f, 500.0f, 0.0f);
        const SphereHitResult k_hr = sphereCast(2.0f, k_feetPos, k_downEnd, world);
        if (k_hr.hit) {
            result.groundDistance = k_hr.t * 500.0f;
        }
    }

    return result;
}

} // namespace physics
