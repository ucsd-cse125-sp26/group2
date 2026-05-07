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
#include "ecs/systems/PickupGeometry.hpp"
#include "entt/entity/entity.hpp"

namespace systems
{

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
            if (weapon.primary.type == spawner.type) {
                weapon.primary.totalAmmo = config.defaultAmmoCapacity;
                weapon.primary.currentMagAmmo = config.magazineSize;
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
            } else if (weapon.secondary.type == spawner.type) {
                weapon.secondary.totalAmmo = config.defaultAmmoCapacity;
                weapon.secondary.currentMagAmmo = config.magazineSize;
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
            }
        }

        const float spawnEyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.77f * spawnEyeDir, 0.0f};
        const glm::vec3 viewFwd = viewForward(input.yaw, input.pitch);

        if (spawner.hasWeapon && input.pickup && isPlayerLookingAtPickup(eye, viewFwd, spawnerPos.value)) {
            // pickup
            spawner.hasWeapon = false;
            spawner.spawnCooldown = weaponCooldownTime;
            const WeaponConfig& config = getWeaponConfig(spawner.type);
            if (weapon.current == WeaponSlot::PRIMARY) {
                weapon.primary = GunInstance{
                    .type = spawner.type,
                    .totalAmmo = config.defaultAmmoCapacity,
                    .currentMagAmmo = config.magazineSize,
                    .fireCooldown = 0.0f,
                };
            } else {
                weapon.secondary = GunInstance{
                    .type = spawner.type,
                    .totalAmmo = config.defaultAmmoCapacity,
                    .currentMagAmmo = config.magazineSize,
                    .fireCooldown = 0.0f,
                };
            }
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