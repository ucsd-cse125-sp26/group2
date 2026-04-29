/// @file Raycast.hpp
/// @brief Shared hitscan raycasting against world geometry and player hitboxes.
///
/// Used by both the server (WeaponSystem) and the client (local fire VFX).
/// Functions are inline so this remains a header-only utility.

#pragma once

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/physics/TriMeshCollision.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/registry/Registry.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace physics
{

/// @brief Maximum hitscan distance in world units.
inline constexpr float k_hitscanRange = 5000.0f;

/// @brief Epsilon for parallel-ray checks.
inline constexpr float k_parallelEpsilon = 1e-6f;

/// @brief Result of a hitscan raycast.
struct HitscanHit
{
    bool hit{false};
    float distance{k_hitscanRange};
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    SurfaceType surface{SurfaceType::Concrete};
    entt::entity entity{entt::null};
};

/// @brief Ray vs axis-aligned box intersection (slab method).
inline bool raycastAABB(glm::vec3 origin,
                        glm::vec3 direction,
                        const WorldAABB& box,
                        float maxDistance,
                        float& outDistance,
                        glm::vec3& outNormal)
{
    float tMin = 0.0f;
    float tMax = maxDistance;
    glm::vec3 hitNormal{0.0f};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < k_parallelEpsilon) {
            if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) {
                return false;
            }
            continue;
        }

        const float invDir = 1.0f / direction[axis];
        float t1 = (box.min[axis] - origin[axis]) * invDir;
        float t2 = (box.max[axis] - origin[axis]) * invDir;
        glm::vec3 axisNormal{0.0f};
        axisNormal[axis] = (invDir >= 0.0f) ? -1.0f : 1.0f;

        if (t1 > t2) {
            std::swap(t1, t2);
            axisNormal = -axisNormal;
        }

        if (t1 > tMin) {
            tMin = t1;
            hitNormal = axisNormal;
        }

        tMax = std::min(tMax, t2);
        if (tMin > tMax) {
            return false;
        }
    }

    if (tMin < 0.0f || tMin > maxDistance) {
        return false;
    }

    outDistance = tMin;
    outNormal = hitNormal;
    return true;
}

/// @brief Ray vs vertical cylinder intersection.
inline bool raycastCylinder(glm::vec3 origin,
                            glm::vec3 direction,
                            const WorldCylinder& cyl,
                            float maxDistance,
                            float& outDistance,
                            glm::vec3& outNormal)
{
    // XZ circle test
    const float ox = origin.x - cyl.base.x;
    const float oz = origin.z - cyl.base.z;
    const float dx = direction.x;
    const float dz = direction.z;

    const float a = dx * dx + dz * dz;
    const float b = 2.0f * (ox * dx + oz * dz);
    const float c = ox * ox + oz * oz - cyl.radius * cyl.radius;

    float tSide = maxDistance + 1.0f;
    glm::vec3 sideNormal{0.0f};

    if (a > k_parallelEpsilon) {
        const float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            const float t = (-b - std::sqrt(disc)) / (2.0f * a);
            if (t >= 0.0f && t < maxDistance) {
                // Check Y bounds at hit point
                const float hitY = origin.y + direction.y * t;
                if (hitY >= cyl.base.y && hitY <= cyl.base.y + cyl.height) {
                    tSide = t;
                    sideNormal = glm::vec3(
                        origin.x + direction.x * t - cyl.base.x, 0.0f, origin.z + direction.z * t - cyl.base.z);
                    const float len = glm::length(sideNormal);
                    if (len > k_parallelEpsilon)
                        sideNormal /= len;
                    else
                        sideNormal = glm::vec3(1.0f, 0.0f, 0.0f);
                }
            }
        }
    }

    // Cap tests (two discs)
    float tCap = maxDistance + 1.0f;
    glm::vec3 capNormal{0.0f};

    if (std::abs(direction.y) > k_parallelEpsilon) {
        // Bottom cap
        float t = (cyl.base.y - origin.y) / direction.y;
        if (t >= 0.0f && t < maxDistance) {
            float hx = origin.x + direction.x * t - cyl.base.x;
            float hz = origin.z + direction.z * t - cyl.base.z;
            if (hx * hx + hz * hz <= cyl.radius * cyl.radius && t < tCap) {
                tCap = t;
                capNormal = glm::vec3(0.0f, -1.0f, 0.0f);
            }
        }
        // Top cap
        t = (cyl.base.y + cyl.height - origin.y) / direction.y;
        if (t >= 0.0f && t < maxDistance) {
            float hx = origin.x + direction.x * t - cyl.base.x;
            float hz = origin.z + direction.z * t - cyl.base.z;
            if (hx * hx + hz * hz <= cyl.radius * cyl.radius && t < tCap) {
                tCap = t;
                capNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            }
        }
    }

    float best = maxDistance + 1.0f;
    glm::vec3 bestN{0.0f};
    if (tSide < best) {
        best = tSide;
        bestN = sideNormal;
    }
    if (tCap < best) {
        best = tCap;
        bestN = capNormal;
    }

    if (best > maxDistance)
        return false;

    outDistance = best;
    outNormal = bestN;
    return true;
}

/// @brief Ray vs sphere intersection.
inline bool raycastSphere(glm::vec3 origin,
                          glm::vec3 direction,
                          const WorldSphere& sph,
                          float maxDistance,
                          float& outDistance,
                          glm::vec3& outNormal)
{
    const glm::vec3 oc = origin - sph.center;
    const float a = glm::dot(direction, direction);
    if (a < k_parallelEpsilon)
        return false;
    const float b = 2.0f * glm::dot(oc, direction);
    const float c = glm::dot(oc, oc) - sph.radius * sph.radius;

    if (c <= 0.0f)
        return false; // inside sphere

    const float disc = b * b - 4.0f * a * c;
    if (disc < 0.0f)
        return false;

    const float t = (-b - std::sqrt(disc)) / (2.0f * a);
    if (t < 0.0f || t > maxDistance)
        return false;

    outDistance = t;
    const glm::vec3 hitPos = origin + direction * t;
    outNormal = glm::normalize(hitPos - sph.center);
    return true;
}

/// @brief Raycast against a triangle mesh using BVH-accelerated Möller-Trumbore.
inline bool raycastTriMesh(glm::vec3 origin,
                           glm::vec3 direction,
                           const WorldTriMesh& mesh,
                           float maxDistance,
                           float& outDistance,
                           glm::vec3& outNormal)
{
    // Quick reject against mesh AABB.
    float dummyDist = maxDistance;
    glm::vec3 dummyN{0.0f};
    const WorldAABB meshBounds{mesh.boundsMin, mesh.boundsMax};
    if (!raycastAABB(origin, direction, meshBounds, maxDistance, dummyDist, dummyN))
        return false;

    bool anyHit = false;
    float bestDist = maxDistance;
    glm::vec3 bestNormal{0.0f};

    int stack[64];
    int stackPtr = 0;
    stack[0] = 0;

    while (stackPtr >= 0) {
        const int nodeIdx = stack[stackPtr--];
        const auto& node = mesh.bvhNodes[static_cast<size_t>(nodeIdx)];

        // Test ray against node AABB.
        const WorldAABB nodeBox{node.boundsMin, node.boundsMax};
        float nodeDist = bestDist;
        glm::vec3 nodeN{0.0f};
        if (!raycastAABB(origin, direction, nodeBox, bestDist, nodeDist, nodeN))
            continue;

        if (node.count > 0) {
            // Leaf — Möller-Trumbore per triangle.
            for (int i = node.leftFirst; i < node.leftFirst + node.count; ++i) {
                const uint32_t ti = mesh.triIndices[static_cast<size_t>(i)];
                const glm::vec3& v0 = mesh.vertices[mesh.indices[ti * 3 + 0]];
                const glm::vec3& v1 = mesh.vertices[mesh.indices[ti * 3 + 1]];
                const glm::vec3& v2 = mesh.vertices[mesh.indices[ti * 3 + 2]];

                const glm::vec3 e1 = v1 - v0;
                const glm::vec3 e2 = v2 - v0;
                const glm::vec3 h = glm::cross(direction, e2);
                const float a = glm::dot(e1, h);
                if (std::abs(a) < 1e-8f)
                    continue;

                const float f = 1.0f / a;
                const glm::vec3 s = origin - v0;
                const float u = f * glm::dot(s, h);
                if (u < 0.0f || u > 1.0f)
                    continue;

                const glm::vec3 q = glm::cross(s, e1);
                const float v = f * glm::dot(direction, q);
                if (v < 0.0f || u + v > 1.0f)
                    continue;

                const float t = f * glm::dot(e2, q);
                if (t > 0.0f && t < bestDist) {
                    bestDist = t;
                    bestNormal = glm::normalize(glm::cross(e1, e2));
                    if (glm::dot(bestNormal, direction) > 0.0f)
                        bestNormal = -bestNormal;
                    anyHit = true;
                }
            }
        } else {
            stack[++stackPtr] = node.leftFirst;
            stack[++stackPtr] = node.leftFirst + 1;
        }
    }

    if (anyHit) {
        outDistance = bestDist;
        outNormal = bestNormal;
    }
    return anyHit;
}

/// @brief Raycast against all static world geometry (planes + boxes + cylinders + spheres).
inline HitscanHit raycastWorld(glm::vec3 origin, glm::vec3 direction, const WorldGeometry& world)
{
    HitscanHit bestHit;

    for (const Plane& plane : world.planes) {
        const float denom = glm::dot(plane.normal, direction);
        if (std::abs(denom) < k_parallelEpsilon) {
            continue;
        }

        const float distance = (plane.distance - glm::dot(plane.normal, origin)) / denom;
        if (distance < 0.0f || distance >= bestHit.distance) {
            continue;
        }

        bestHit.hit = true;
        bestHit.distance = distance;
        bestHit.point = origin + direction * distance;
        bestHit.normal = plane.normal;
        bestHit.surface = SurfaceType::Concrete;
    }

    for (const WorldAABB& box : world.boxes) {
        float distance = bestHit.distance;
        glm::vec3 normal{0.0f};
        if (!raycastAABB(origin, direction, box, bestHit.distance, distance, normal)) {
            continue;
        }

        bestHit.hit = true;
        bestHit.distance = distance;
        bestHit.point = origin + direction * distance;
        bestHit.normal = normal;
        bestHit.surface = SurfaceType::Concrete;
    }

    for (const WorldCylinder& cyl : world.cylinders) {
        float distance = bestHit.distance;
        glm::vec3 normal{0.0f};
        if (!raycastCylinder(origin, direction, cyl, bestHit.distance, distance, normal))
            continue;
        bestHit.hit = true;
        bestHit.distance = distance;
        bestHit.point = origin + direction * distance;
        bestHit.normal = normal;
        bestHit.surface = SurfaceType::Concrete;
    }

    for (const WorldSphere& sph : world.spheres) {
        float distance = bestHit.distance;
        glm::vec3 normal{0.0f};
        if (!raycastSphere(origin, direction, sph, bestHit.distance, distance, normal))
            continue;
        bestHit.hit = true;
        bestHit.distance = distance;
        bestHit.point = origin + direction * distance;
        bestHit.normal = normal;
        bestHit.surface = SurfaceType::Concrete;
    }

    for (const WorldTriMesh& tm : world.triMeshes) {
        float distance = bestHit.distance;
        glm::vec3 normal{0.0f};
        if (!raycastTriMesh(origin, direction, tm, bestHit.distance, distance, normal))
            continue;
        bestHit.hit = true;
        bestHit.distance = distance;
        bestHit.point = origin + direction * distance;
        bestHit.normal = normal;
        bestHit.surface = SurfaceType::Concrete;
    }

    return bestHit;
}

/// @brief Raycast against all player hitboxes (axis-aligned capsule approximation).
inline HitscanHit
raycastPlayers(Registry& registry, entt::entity shooter, glm::vec3 origin, glm::vec3 direction, float maxDistance)
{
    HitscanHit bestHit;
    bestHit.distance = maxDistance;

    registry.view<Position, Player, CollisionShape>().each(
        [&](const entt::entity entity, const Position& pos, const CollisionShape& shape) {
            if (entity == shooter) {
                return;
            }

            const WorldAABB bounds{
                .min = pos.value - shape.halfExtents,
                .max = pos.value + shape.halfExtents,
            };

            float distance = bestHit.distance;
            glm::vec3 normal{0.0f};
            if (!raycastAABB(origin, direction, bounds, bestHit.distance, distance, normal)) {
                return;
            }

            bestHit.hit = true;
            bestHit.distance = distance;
            bestHit.point = origin + direction * distance;
            bestHit.normal = normal;
            bestHit.surface = SurfaceType::Flesh;
            bestHit.entity = entity;
        });

    return bestHit;
}

/// @brief Full hitscan resolution: world geometry first, then players (closest wins).
inline HitscanHit resolveHitscan(Registry& registry, entt::entity shooter, glm::vec3 origin, glm::vec3 direction)
{
    HitscanHit bestHit = raycastWorld(origin, direction, activeWorld());

    const HitscanHit playerHit = raycastPlayers(registry, shooter, origin, direction, bestHit.distance);
    if (playerHit.hit && (!bestHit.hit || playerHit.distance < bestHit.distance)) {
        bestHit = playerHit;
    }

    if (!bestHit.hit) {
        bestHit.distance = k_hitscanRange;
        bestHit.point = origin + direction * k_hitscanRange;
    }

    return bestHit;
}

} // namespace physics
