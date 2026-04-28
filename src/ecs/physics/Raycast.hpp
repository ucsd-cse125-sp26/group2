/// @file Raycast.hpp
/// @brief Shared hitscan raycasting against world geometry and player hitboxes.
///
/// Used by both the server (WeaponSystem) and the client (local fire VFX).
/// Functions are inline so this remains a header-only utility.

#pragma once

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/physics/SweptCollision.hpp"
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

/// @brief Raycast against all static world geometry (planes + boxes).
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

    return bestHit;
}

/// @brief Raycast against all player hitboxes (axis-aligned capsule approximation).
inline HitscanHit
raycastPlayers(Registry& registry, entt::entity shooter, glm::vec3 origin, glm::vec3 direction, float maxDistance)
{
    HitscanHit bestHit;
    bestHit.distance = maxDistance;

    registry.view<Position, CollisionShape>().each(
        [&](entt::entity entity, const Position& pos, const CollisionShape& shape) {
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
