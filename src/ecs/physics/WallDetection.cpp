/// @file WallDetection.cpp
/// @brief Implementation of wall detection.

#include "WallDetection.hpp"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace physics
{

namespace
{

struct MeshWallProbe
{
    bool hit{false};
    float score{1e30f};
    glm::vec3 normal{0.0f};
    glm::vec3 point{0.0f};
    uint32_t meshIndex{UINT32_MAX};
    uint32_t triId{UINT32_MAX};
    TriRegion region{TriRegion::Face};
};

int edgeIndexForRegion(TriRegion region)
{
    switch (region) {
    case TriRegion::Edge0:
        return 0;
    case TriRegion::Edge1:
        return 1;
    case TriRegion::Edge2:
        return 2;
    default:
        return -1;
    }
}

int incidentEdgeForVertex(TriRegion vertexRegion, int slot)
{
    switch (vertexRegion) {
    case TriRegion::Vert0:
        return slot == 0 ? 2 : 0;
    case TriRegion::Vert1:
        return slot == 0 ? 0 : 1;
    case TriRegion::Vert2:
        return slot == 0 ? 1 : 2;
    default:
        return -1;
    }
}

WorldAABB wallProbeBounds(glm::vec3 segA, glm::vec3 segB, glm::vec3 dir, float checkDist, float sphereRadius)
{
    const glm::vec3 endA = segA + dir * checkDist;
    const glm::vec3 endB = segB + dir * checkDist;
    const glm::vec3 minAB = glm::min(glm::min(segA, segB), glm::min(endA, endB)) - glm::vec3(sphereRadius);
    const glm::vec3 maxAB = glm::max(glm::max(segA, segB), glm::max(endA, endB)) + glm::vec3(sphereRadius);
    return {.min = minAB, .max = maxAB};
}

MeshWallProbe probeTriMeshWalls(glm::vec3 pos,
                                glm::vec3 dir,
                                glm::vec3 halfExtents,
                                const WorldGeometry& world,
                                float checkDist,
                                float sphereRadius)
{
    MeshWallProbe best;
    if (glm::dot(dir, dir) < 1e-8f)
        return best;

    dir = glm::normalize(dir);

    // Query the player's vertical body segment, not a single sphere. This
    // keeps wallrun attachment stable when the player is already within
    // the probe radius or crosses a seam between adjacent wall triangles.
    const float axisHalfHeight = std::max(halfExtents.y - sphereRadius, 0.0f);
    const glm::vec3 segA = pos + glm::vec3{0.0f, axisHalfHeight, 0.0f};
    const glm::vec3 segB = pos - glm::vec3{0.0f, axisHalfHeight, 0.0f};
    const float maxAxisDistance = checkDist + sphereRadius;

    auto considerMesh = [&](uint32_t meshIndex, const WorldTriMesh& tm) {
        const ClosestPointOnMeshResult cp = closestPointOnMesh(segA, segB, maxAxisDistance, tm);
        if (!cp.found || !isWallNormal(cp.normal))
            return;

        const float alongProbe = glm::dot(cp.pointOnMesh - pos, dir);
        if (alongProbe < -sphereRadius || alongProbe > checkDist + sphereRadius)
            return;

        const float surfaceDistance = std::max(cp.dist - sphereRadius, 0.0f);
        const float score = surfaceDistance + std::max(alongProbe, 0.0f) * 0.001f;
        if (score >= best.score)
            return;

        best.hit = true;
        best.score = score;
        best.normal = cp.normal;
        best.point = cp.pointOnMesh;
        best.meshIndex = meshIndex;
        best.triId = cp.triId;
        best.region = cp.region;
    };

    const WorldAABB query = wallProbeBounds(segA, segB, dir, checkDist, sphereRadius);
    if (world.staticBroadphase != nullptr && !world.staticBroadphase->nodes.empty()) {
        queryStaticWorldBroadphase(*world.staticBroadphase, query, [&](uint32_t meshIndex) {
            if (meshIndex < world.triMeshes.size())
                considerMesh(meshIndex, world.triMeshes[meshIndex]);
            return true;
        });
    } else {
        for (uint32_t i = 0; i < static_cast<uint32_t>(world.triMeshes.size()); ++i)
            considerMesh(i, world.triMeshes[i]);
    }

    return best;
}

} // namespace

WallAttachmentResult findWallRunAttachment(CapsuleShape capsule,
                                           glm::vec3 pos,
                                           const WorldGeometry& world,
                                           glm::vec3 continuityNormal,
                                           glm::vec3 travelDir,
                                           float lookaheadDist,
                                           float checkDist,
                                           uint32_t previousMeshIndex,
                                           uint32_t previousTriId,
                                           TriRegion previousRegion)
{
    WallAttachmentResult best;
    float bestScore = 1e30f;
    const float maxAxisDist = capsule.radius + checkDist + 8.0f;
    const bool hasContinuity = glm::length(continuityNormal) > 0.5f;
    const bool hasTravel = glm::length(travelDir) > 0.5f;
    const bool hasLookahead = hasTravel && lookaheadDist > 0.0f;
    const glm::vec3 travel = hasTravel ? glm::normalize(travelDir) : glm::vec3{0.0f};
    const glm::vec3 lookaheadPos = hasLookahead ? pos + travel * lookaheadDist : pos;

    auto considerClosest =
        [&](uint32_t meshIndex, const ClosestPointOnMeshResult& cp, bool lookaheadSample, float scoreBias = 0.0f) {
            if (!cp.found || !isWallNormal(cp.normal))
                return;
            if (cp.dist > maxAxisDist)
                return;

            const float continuity = hasContinuity ? glm::dot(cp.normal, continuityNormal) : 1.0f;
            if (continuity < -0.05f)
                return;

            const float blocking = hasTravel ? std::max(0.0f, -glm::dot(travel, cp.normal)) : 0.0f;
            const float surfaceDist = std::abs(cp.dist - capsule.minkowskiExtent(cp.normal));
            const float continuityPenalty = (1.0f - continuity) * 2.0f;
            const float lookaheadBonus = lookaheadSample ? lookaheadDist * 0.5f : 0.0f;
            const float blockingScale = std::max(lookaheadDist, capsule.radius * 0.5f);
            const float blockingBonus = blocking * blockingScale * 2.0f;
            const float score = surfaceDist + continuityPenalty - lookaheadBonus - blockingBonus + scoreBias;
            if (score >= bestScore)
                return;

            bestScore = score;
            best.found = true;
            best.anchor = cp.pointOnMesh;
            best.normal = cp.normal;
            best.meshIndex = meshIndex;
            best.triId = cp.triId;
            best.region = cp.region;
        };

    auto considerNeighbor = [&](uint32_t meshIndex, const WorldTriMesh& mesh, uint32_t neighborTri) {
        if (neighborTri == UINT32_MAX || neighborTri == previousTriId)
            return;

        const ClosestPointOnMeshResult cp = closestPointOnMeshTriangle(capsule, pos, maxAxisDist, mesh, neighborTri);
        if (!cp.found)
            return;
        if (hasTravel && glm::dot(travel, cp.normal) > 0.25f)
            return;
        considerClosest(meshIndex, cp, false, -capsule.radius);

        if (hasLookahead) {
            const ClosestPointOnMeshResult lookaheadCp =
                closestPointOnMeshTriangle(capsule, lookaheadPos, maxAxisDist, mesh, neighborTri);
            considerClosest(meshIndex, lookaheadCp, true, -capsule.radius);
        }
    };

    auto considerPreviousTriangleAdjacency = [&](uint32_t meshIndex, const WorldTriMesh& mesh) {
        if (meshIndex != previousMeshIndex || previousTriId == UINT32_MAX)
            return;

        const ClosestPointOnMeshResult prevCp =
            closestPointOnMeshTriangle(capsule, pos, maxAxisDist, mesh, previousTriId);
        if (!prevCp.found)
            return;

        auto considerSeamRegion = [&](TriRegion seamRegion) {
            const int edge = edgeIndexForRegion(seamRegion);
            if (edge >= 0) {
                const size_t neighborIndex = static_cast<size_t>(previousTriId) * 3u + static_cast<size_t>(edge);
                if (neighborIndex < mesh.edgeNeighbor.size())
                    considerNeighbor(meshIndex, mesh, mesh.edgeNeighbor[neighborIndex]);
                return;
            }

            for (int slot = 0; slot < 2; ++slot) {
                const int vertexEdge = incidentEdgeForVertex(seamRegion, slot);
                if (vertexEdge < 0)
                    continue;
                const size_t neighborIndex = static_cast<size_t>(previousTriId) * 3u + static_cast<size_t>(vertexEdge);
                if (neighborIndex < mesh.edgeNeighbor.size())
                    considerNeighbor(meshIndex, mesh, mesh.edgeNeighbor[neighborIndex]);
            }
        };

        considerSeamRegion(prevCp.region);
        if (previousRegion != prevCp.region)
            considerSeamRegion(previousRegion);
    };

    auto considerMesh = [&](uint32_t meshIndex, const WorldTriMesh& mesh) {
        considerClosest(meshIndex, closestPointOnMesh(capsule, pos, maxAxisDist, mesh), false);
        if (hasLookahead)
            considerClosest(meshIndex, closestPointOnMesh(capsule, lookaheadPos, maxAxisDist, mesh), true);
        considerPreviousTriangleAdjacency(meshIndex, mesh);
    };

    const glm::vec3 queryMin = glm::min(pos, lookaheadPos) - capsule.enclosingHalfExtents() - glm::vec3(maxAxisDist);
    const glm::vec3 queryMax = glm::max(pos, lookaheadPos) + capsule.enclosingHalfExtents() + glm::vec3(maxAxisDist);
    const WorldAABB query{.min = queryMin, .max = queryMax};
    if (world.staticBroadphase != nullptr && !world.staticBroadphase->nodes.empty()) {
        queryStaticWorldBroadphase(*world.staticBroadphase, query, [&](uint32_t meshIndex) {
            if (meshIndex < world.triMeshes.size())
                considerMesh(meshIndex, world.triMeshes[meshIndex]);
            return true;
        });
    } else {
        for (uint32_t i = 0; i < static_cast<uint32_t>(world.triMeshes.size()); ++i)
            considerMesh(i, world.triMeshes[i]);
    }

    return best;
}

WallDetectionResult detectWalls(glm::vec3 pos,
                                float yaw,
                                glm::vec3 halfExtents,
                                const WorldGeometry& world,
                                float checkDist,
                                float sphereRadius,
                                glm::vec3 prevWallNormal,
                                bool gravityFlipped)
{
    WallDetectionResult result;
    const glm::vec3 worldUp{0.0f, gravityFlipped ? -1.0f : 1.0f, 0.0f};

    // Player's local axes in world space.
    const float k_sinYaw = std::sin(yaw);
    const float k_cosYaw = std::cos(yaw);
    const glm::vec3 k_forward{k_sinYaw, 0.0f, k_cosYaw};
    const glm::vec3 k_right{k_cosYaw, 0.0f, -k_sinYaw};

    // Side wall detection (for wallrunning)
    // Static trimeshes use body-segment closest-point queries so already
    // attached wallruns do not flicker when the probe starts overlapped.
    // Primitive/debug geometry keeps the sphere-cast fallback.
    {
        const MeshWallProbe meshHit = probeTriMeshWalls(pos, k_right, halfExtents, world, checkDist, sphereRadius);
        if (meshHit.hit) {
            result.wallRight = true;
            result.rightNormal = meshHit.normal;
            result.rightPoint = meshHit.point;
            result.rightMeshIndex = meshHit.meshIndex;
            result.rightTriId = meshHit.triId;
            result.rightRegion = meshHit.region;
        } else {
            const glm::vec3 k_rightEnd = pos + k_right * checkDist;
            const SphereHitResult k_hr = sphereCast(sphereRadius, pos, k_rightEnd, world);
            if (k_hr.hit && isWallNormal(k_hr.normal)) {
                result.wallRight = true;
                result.rightNormal = k_hr.normal;
                result.rightPoint = k_hr.point;
            }
        }
    }
    {
        const MeshWallProbe meshHit = probeTriMeshWalls(pos, -k_right, halfExtents, world, checkDist, sphereRadius);
        if (meshHit.hit) {
            result.wallLeft = true;
            result.leftNormal = meshHit.normal;
            result.leftPoint = meshHit.point;
            result.leftMeshIndex = meshHit.meshIndex;
            result.leftTriId = meshHit.triId;
            result.leftRegion = meshHit.region;
        } else {
            const glm::vec3 k_leftEnd = pos - k_right * checkDist;
            const SphereHitResult k_hr = sphereCast(sphereRadius, pos, k_leftEnd, world);
            if (k_hr.hit && isWallNormal(k_hr.normal)) {
                result.wallLeft = true;
                result.leftNormal = k_hr.normal;
                result.leftPoint = k_hr.point;
            }
        }
    }

    // Curved surface tracking: when we have a previous wall normal (active
    // wallrun), cast toward the wall using -prevWallNormal.  This catches
    // curved surfaces (cylinders, concave walls) where the standard left/right
    // trace might overshoot because the surface normal has rotated.
    if (glm::length(prevWallNormal) > 0.5f) {
        const glm::vec3 k_towardWall = -prevWallNormal;
        MeshWallProbe meshHit = probeTriMeshWalls(pos, k_towardWall, halfExtents, world, checkDist, sphereRadius);

        if (meshHit.hit) {
            // Determine which side of the player this wall is on by checking
            // the dot product of the hit normal with the player's right axis.
            const float k_side = glm::dot(meshHit.normal, k_right);

            if (k_side < -0.1f) {
                // Wall normal points opposite to player right → wall is to the right
                if (!result.wallRight || meshHit.score < 0.5f) {
                    result.wallRight = true;
                    result.rightNormal = meshHit.normal;
                    result.rightPoint = meshHit.point;
                    result.rightMeshIndex = meshHit.meshIndex;
                    result.rightTriId = meshHit.triId;
                    result.rightRegion = meshHit.region;
                }
            } else if (k_side > 0.1f) {
                // Wall normal points along player right → wall is to the left
                if (!result.wallLeft || meshHit.score < 0.5f) {
                    result.wallLeft = true;
                    result.leftNormal = meshHit.normal;
                    result.leftPoint = meshHit.point;
                    result.leftMeshIndex = meshHit.meshIndex;
                    result.leftTriId = meshHit.triId;
                    result.leftRegion = meshHit.region;
                }
            } else {
                // Wall is roughly in front/behind — assign to whichever side
                // matches the previous wall normal more closely.
                const float k_rightDot = glm::dot(prevWallNormal, result.rightNormal);
                const float k_leftDot = glm::dot(prevWallNormal, result.leftNormal);
                if (k_rightDot > k_leftDot) {
                    result.wallRight = true;
                    result.rightNormal = meshHit.normal;
                    result.rightPoint = meshHit.point;
                    result.rightMeshIndex = meshHit.meshIndex;
                    result.rightTriId = meshHit.triId;
                    result.rightRegion = meshHit.region;
                } else {
                    result.wallLeft = true;
                    result.leftNormal = meshHit.normal;
                    result.leftPoint = meshHit.point;
                    result.leftMeshIndex = meshHit.meshIndex;
                    result.leftTriId = meshHit.triId;
                    result.leftRegion = meshHit.region;
                }
            }
        } else {
            const glm::vec3 k_traceEnd = pos + k_towardWall * checkDist;
            const SphereHitResult k_hr = sphereCast(sphereRadius, pos, k_traceEnd, world);

            if (k_hr.hit && isWallNormal(k_hr.normal)) {
                const float k_side = glm::dot(k_hr.normal, k_right);

                if (k_side < -0.1f) {
                    if (!result.wallRight || k_hr.t < 0.5f) {
                        result.wallRight = true;
                        result.rightNormal = k_hr.normal;
                        result.rightPoint = k_hr.point;
                    }
                } else if (k_side > 0.1f) {
                    if (!result.wallLeft || k_hr.t < 0.5f) {
                        result.wallLeft = true;
                        result.leftNormal = k_hr.normal;
                        result.leftPoint = k_hr.point;
                    }
                } else {
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
    }

    // Front wall detection for diagnostics.
    {
        const MeshWallProbe meshHit = probeTriMeshWalls(pos, k_forward, halfExtents, world, checkDist, sphereRadius);
        if (meshHit.hit) {
            result.wallFront = true;
            result.frontNormal = meshHit.normal;
            result.frontPoint = meshHit.point;
            result.frontMeshIndex = meshHit.meshIndex;
            result.frontTriId = meshHit.triId;
            result.frontRegion = meshHit.region;
        } else {
            const glm::vec3 k_frontEnd = pos + k_forward * checkDist;
            const SphereHitResult k_hr = sphereCast(sphereRadius, pos, k_frontEnd, world);
            if (k_hr.hit && isWallNormal(k_hr.normal)) {
                result.wallFront = true;
                result.frontNormal = k_hr.normal;
                result.frontPoint = k_hr.point;
            }
        }
    }

    // Ground distance probe
    {
        const CapsuleShape groundProbeCapsule{
            .radius = sphereRadius,
            .halfHeight = std::max(halfExtents.y - sphereRadius, 0.0f),
            .up = worldUp,
        };
        const GroundProbeResult ground = probeGround(groundProbeCapsule, pos, 500.0f, world);
        if (ground.hit)
            result.groundDistance = ground.distance;
    }

    return result;
}

} // namespace physics
