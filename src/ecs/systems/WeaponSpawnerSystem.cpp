/// @file WeaponSpawnerSystem.cpp
/// @brief Weapon spawner manager system.

#include "WeaponSpawnerSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/GrenadeConfig.hpp"
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
    view.each([&](entt::entity /*player*/,
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
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
            } else if (secondary.type == spawner.type) {
                secondary.totalAmmo = config.defaultAmmoCapacity;
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
            }
        }

        const float spawnEyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.77f * spawnEyeDir, 0.0f};
        const glm::vec3 viewFwd = viewForward(input.yaw, input.pitch);

        if (spawner.hasWeapon && input.pickup && isPlayerLookingAtPickup(eye, viewFwd, spawnerPos.value)) {
            // Resolve the destination slot under the type-compatibility guard.
            // The currently-equipped slot is preferred so picking up a rifle
            // while holding a rifle replaces it in-place; otherwise we fall
            // back to PRIMARY. Grenades are not weapon-slot pickups; they live
            // in GrenadeState.
            const WeaponSlot targetSlot =
                canAcceptType(weapon.current, spawner.type) ? weapon.current : WeaponSlot::PRIMARY;
            if (!canAcceptType(targetSlot, spawner.type)) {
                // Fallback slot can't accept this type either (e.g. a
                // hypothetical world-spawned grenade hitting this path).
                // Reject the pickup rather than silently dropping it into a gun slot.
                return;
            }
            spawner.hasWeapon = false;
            spawner.spawnCooldown = weaponCooldownTime;
            const WeaponConfig& config = getWeaponConfig(spawner.type);
            getSlot(weapon, targetSlot) = GunInstance{
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
    view.each([&](WeaponSpawner& spawner, const Position& pos, const CollisionShape& shape) {
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
