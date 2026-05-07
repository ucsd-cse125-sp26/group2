/// @file WeaponSpawnerSystem.cpp
/// @brief Weapon spawner manager system.

#pragma once
#include "WeaponSpawnerSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/registry/Registry.hpp"
#include "entt/entity/entity.hpp"

namespace systems
{

/// @brief Test whether two axis-aligned bounding boxes overlap.
/// @param aPos   Center of box A.
/// @param aHalf  Half-extents of box A.
/// @param bPos   Center of box B.
/// @param bHalf  Half-extents of box B.
/// @return True if the boxes overlap on all three axes.
inline bool overlapsAABB(glm::vec3 aPos, glm::vec3 aHalf, glm::vec3 bPos, glm::vec3 bHalf)
{
    return std::abs(aPos.x - bPos.x) <= (aHalf.x + bHalf.x) && std::abs(aPos.y - bPos.y) <= (aHalf.y + bHalf.y) &&
           std::abs(aPos.z - bPos.z) <= (aHalf.z + bHalf.z);
}

inline glm::vec3 viewForward(float yaw, float pitch)
{
    const float cp = std::cos(pitch);
    return glm::normalize(glm::vec3{
        std::sin(yaw) * cp,
        -std::sin(pitch),
        std::cos(yaw) * cp,
    });
}

/// @brief Check if any player overlaps the spawner and transfer the weapon on pickup.
/// @param registry      The ECS registry.
/// @param spawnerPos    Position of the spawner entity.
/// @param spawnerShape  Collision shape of the spawner.
/// @param spawner       Spawner component (weapon type, availability, cooldown).
inline void
checkForPlayers(Registry& registry, Position spawnerPos, CollisionShape spawnerShape, WeaponSpawner& spawner)
{
    auto view = registry.view<Player, Position, CollisionShape, InputSnapshot, WeaponState, PlayerVisState>();
    view.each([&](entt::entity player,
                  const Position& pos,
                  const CollisionShape& shape,
                  const InputSnapshot& input,
                  WeaponState& weapon,
                  const PlayerVisState& pvis) {
        if (overlapsAABB(spawnerPos.value, spawnerShape.halfExtents, pos.value, shape.halfExtents) && spawner.hasWeapon)
        {
            const WeaponConfig config = getWeaponConfig(spawner.type);
            GunInstance& primary = getSlot(weapon, WeaponSlot::PRIMARY);
            GunInstance& secondary = getSlot(weapon, WeaponSlot::SECONDARY);
            if (primary.type == spawner.type) {
                primary.totalAmmo = config.defaultAmmoCapacity;
                primary.currentMagAmmo = config.magazineSize;
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
            } else if (secondary.type == spawner.type) {
                secondary.totalAmmo = config.defaultAmmoCapacity;
                secondary.currentMagAmmo = config.magazineSize;
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
            }
        }

        static constexpr float k_pickupRange = 140.0f;
        static constexpr float k_pickupMaxAngleDeg = 12.0f;
        static const float k_pickupMinDot = std::cos(glm::radians(k_pickupMaxAngleDeg));

        const float spawnEyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.77f * spawnEyeDir, 0.0f};
        const glm::vec3 toWeapon = spawnerPos.value - eye;
        const float distSq = glm::dot(toWeapon, toWeapon);

        const bool inRange = distSq <= k_pickupRange * k_pickupRange;
        const bool lookingAtWeapon = distSq > 0.0001f && glm::dot(viewForward(input.yaw, input.pitch),
                                                                  glm::normalize(toWeapon)) >= k_pickupMinDot;

        if (spawner.hasWeapon && input.pickup && inRange && lookingAtWeapon) {
            // pickup
            spawner.hasWeapon = false;
            spawner.spawnCooldown = weaponCooldownTime;
            const WeaponConfig& config = getWeaponConfig(spawner.type);
            getEquippedGun(weapon) = GunInstance{
                .type = spawner.type,
                .totalAmmo = config.defaultAmmoCapacity,
                .currentMagAmmo = config.magazineSize,
                .fireCooldown = 0.0f,
            };
        }
    });
}

void runWeaponSpawners(Registry& registry, float dt)
{
    auto view = registry.view<WeaponSpawner, Position, CollisionShape>();
    view.each([&](entt::entity e, WeaponSpawner& spawner, const Position& pos, const CollisionShape& shape) {
        checkForPlayers(registry, pos, shape, spawner);
        if ((spawner.spawnCooldown - dt) > 0.0f) {
            spawner.spawnCooldown -= dt;
        } else {
            spawner.spawnCooldown = 0;
            spawner.hasWeapon = true;
        }
    });
}
} // namespace systems