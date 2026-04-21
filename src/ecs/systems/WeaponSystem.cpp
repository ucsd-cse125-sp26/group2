/// @file WeaponSystem.cpp
/// @brief Weapon state manager system.

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/WorldData.hpp"
#include "ecs/registry/Registry.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace
{

constexpr float k_hitscanRange = 5000.0f;
constexpr float k_parallelEpsilon = 1e-6f;

struct HitscanHit
{
    bool hit{false};
    float distance{k_hitscanRange};
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    SurfaceType surface{SurfaceType::Concrete};
    entt::entity entity{entt::null};
};

bool raycastAABB(glm::vec3 origin,
                 glm::vec3 direction,
                 const physics::WorldAABB& box,
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

HitscanHit raycastWorld(glm::vec3 origin, glm::vec3 direction, const physics::WorldGeometry& world)
{
    HitscanHit bestHit;

    for (const physics::Plane& plane : world.planes) {
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

    for (const physics::WorldAABB& box : world.boxes) {
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

HitscanHit
raycastPlayers(Registry& registry, entt::entity shooter, glm::vec3 origin, glm::vec3 direction, float maxDistance)
{
    HitscanHit bestHit;
    bestHit.distance = maxDistance;

    registry.view<Position, CollisionShape>().each(
        [&](entt::entity entity, const Position& pos, const CollisionShape& shape) {
            if (entity == shooter) {
                return;
            }

            const physics::WorldAABB bounds{
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

HitscanHit resolveHitscan(Registry& registry, entt::entity shooter, glm::vec3 origin, glm::vec3 direction)
{
    HitscanHit bestHit = raycastWorld(origin, direction, physics::testWorld());

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

} // namespace

namespace systems
{

inline GunInstance& getEquippedGun(WeaponState& weapon)
{
    return (weapon.current == WeaponSlot::PRIMARY) ? weapon.primary : weapon.secondary;
}

void handleSwitch(const InputSnapshot& input, WeaponState& weapon)
{
    if (input.switchToPrimary) {
        weapon.current = WeaponSlot::PRIMARY;
    } else if (input.switchToSecondary) {
        weapon.current = WeaponSlot::SECONDARY;
    }
}

inline void handleCooldown(WeaponState& weapon, float dt)
{
    auto reduce = [dt](GunInstance& gun) { gun.fireCooldown = std::max(0.0f, gun.fireCooldown - dt); };

    reduce(weapon.primary);
    reduce(weapon.secondary);
}

inline void handleReload(GunInstance& gun)
{
    const WeaponConfig& config = getWeaponConfig(gun.type);
    if (gun.totalAmmo > 0 && gun.currentMagAmmo < config.magazineSize) {
        int reloadAmount = config.magazineSize - gun.currentMagAmmo;
        if ((gun.totalAmmo - reloadAmount) >= 0) {
            gun.currentMagAmmo += reloadAmount;
            gun.totalAmmo -= reloadAmount;
        } else {
            gun.currentMagAmmo += gun.totalAmmo;
            gun.totalAmmo = 0;
        }
    }
}

inline bool handleAmmo(GunInstance& gun)
{
    if (gun.currentMagAmmo <= 0) {
        // handleReload(gun);
        return false;
    }

    --gun.currentMagAmmo;
    return true;
}

inline glm::vec3 viewForward(float yaw, float pitch)
{
    return glm::normalize(glm::vec3{
        -std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        -std::cos(yaw) * std::cos(pitch),
    });
}

inline void handleFire(Registry& registry,
                       entt::entity shooter,
                       const InputSnapshot& input,
                       const Position& pos,
                       const CollisionShape& shape,
                       WeaponState& weapon)
{
    if (!input.shooting) {
        return;
    }

    GunInstance& gun = getEquippedGun(weapon);
    const WeaponConfig& config = getWeaponConfig(gun.type);

    if (gun.fireCooldown > 0.0f) {
        return;
    }

    if (!handleAmmo(gun)) {
        return;
    }

    // Set the cooldown timer
    gun.fireCooldown = config.fireCooldown;

    if (!config.hitscan) {
        return;
    }

    const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f, 0.0f};
    const glm::vec3 direction = viewForward(input.yaw, input.pitch);
    const HitscanHit hit = resolveHitscan(registry, shooter, eye, direction);

    // TODO: apply damage to hit.entity and emit a replicated shot event for client FX.
    if (hit.entity != entt::null && registry.valid(hit.entity)) {
        Health& playerHealth = registry.get_or_emplace<Health>(hit.entity);
        if (playerHealth.armor >= config.damage) {
            playerHealth.armor -= config.damage;
        } else {
            float overflow = config.damage - playerHealth.armor;
            playerHealth.health -= overflow;
        }
    }
    // SDL_Log("[server] Shot fired by (%d)", shooter);
    // registry.view<InputSnapshot, WeaponState>().each(
    //     [&](entt::entity shooter,
    //         InputSnapshot& input,
    //         WeaponState& weapon) {
    //             SDL_Log("[server] Entity (%d) has (%d) ammo in their primary", shooter,
    //             weapon.primary.currentMagAmmo);
    //     });
}

void runWeapon(Registry& registry, float dt)
{
    registry.view<InputSnapshot, Position, CollisionShape, WeaponState>().each([&](entt::entity shooter,
                                                                                   InputSnapshot& input,
                                                                                   const Position& pos,
                                                                                   const CollisionShape& shape,
                                                                                   WeaponState& weapon) {
        handleSwitch(input, weapon);
        handleCooldown(weapon, dt);
        handleFire(registry, shooter, input, pos, shape, weapon);
        if (input.reload) {
            GunInstance& gun = getEquippedGun(weapon);
            handleReload(gun);
        }
    });
}

} // namespace systems
