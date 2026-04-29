/// @file WeaponSpawnerSystem.cpp
/// @brief Weapon spawner manager system.

#pragma once
#include "WeaponSpawnerSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/registry/Registry.hpp"
#include "entt/entity/entity.hpp"

namespace systems
{

inline bool overlapsAABB(glm::vec3 aPos, glm::vec3 aHalf, glm::vec3 bPos, glm::vec3 bHalf)
{
    return std::abs(aPos.x - bPos.x) <= (aHalf.x + bHalf.x) &&
           std::abs(aPos.y - bPos.y) <= (aHalf.y + bHalf.y) &&
           std::abs(aPos.z - bPos.z) <= (aHalf.z + bHalf.z);
}

inline void checkForPlayers(Registry& registry, Position spawnerPos, CollisionShape spawnerShape, WeaponSpawner spawner)
{
    auto view = registry.view<Player, Position, CollisionShape, InputSnapshot, WeaponState>();
    view.each([&](entt::entity player, const Position& pos, const CollisionShape& shape, const InputSnapshot& input, WeaponState& weapon) {
        if (overlapsAABB(spawnerPos.value,
                         spawnerShape.halfExtents,
                         pos.value,
                         shape.halfExtents) && spawner.hasWeapon && input.pickup) {
            // player is inside / overlapping the spawner
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
    view.each([&](entt::entity e, WeaponSpawner& spawner, const Position& pos, const CollisionShape& shape)
    {
        checkForPlayers(registry, pos, shape, spawner);
        if ((spawner.spawnCooldown - dt) > 0.0f) {
            spawner.spawnCooldown -= dt;
            SDL_Log("%f", spawner.spawnCooldown);
        } else {
            spawner.spawnCooldown = 0;
            spawner.hasWeapon = true;
        }
    });
}
}