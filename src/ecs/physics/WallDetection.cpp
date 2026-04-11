#include "WallDetection.hpp"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace physics
{

WallDetectionResult detectWalls(
    glm::vec3 pos, float yaw, glm::vec3 halfExtents, const WorldGeometry& world, float checkDist, float sphereRadius)
{
    WallDetectionResult result;

    // Player's local axes in world space.
    const float k_sinYaw = std::sin(yaw);
    const float k_cosYaw = std::cos(yaw);
    const glm::vec3 k_forward{k_sinYaw, 0.0f, k_cosYaw};
    const glm::vec3 k_right{k_cosYaw, 0.0f, -k_sinYaw};

    // ── Side wall detection (for wallrunning) ───────────────────────────
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

    // ── Front wall detection (for climbing) ─────────────────────────────
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

    // ── Ledge detection ─────────────────────────────────────────────────
    // If a front wall is detected, check if we're near the top by tracing
    // from above the player's head forward, then down. If the forward trace
    // MISSES but we had a wall below, there's a ledge.
    if (result.wallFront) {
        // Trace forward from above the player's head.
        const glm::vec3 k_headTop = pos + glm::vec3(0.0f, halfExtents.y + 10.0f, 0.0f);
        const glm::vec3 k_headTopFwd = k_headTop + k_forward * checkDist;
        const SphereHitResult k_topFwd = sphereCast(sphereRadius, k_headTop, k_headTopFwd, world);

        if (!k_topFwd.hit) {
            // The wall doesn't extend above our head — there's a ledge.
            // Trace downward from the forward+above position to find the ledge surface.
            const glm::vec3 k_probeStart = k_headTopFwd;
            const glm::vec3 k_probeEnd = k_probeStart - glm::vec3(0.0f, halfExtents.y * 2.0f + 40.0f, 0.0f);
            const SphereHitResult k_downHit = sphereCast(sphereRadius * 0.5f, k_probeStart, k_probeEnd, world);

            if (k_downHit.hit && k_downHit.normal.y > 0.7f) {
                result.ledgeDetected = true;
                result.ledgePoint = k_downHit.point;
                result.ledgeNormal = result.frontNormal; // wall normal at the ledge
            }
        }
    }

    // ── Ground distance probe ───────────────────────────────────────────
    // Cast straight down from the player's feet to measure height above ground.
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
