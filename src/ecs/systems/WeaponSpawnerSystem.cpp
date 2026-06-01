/// @file WeaponSpawnerSystem.cpp
/// @brief Weapon spawner manager system.

#include "WeaponSpawnerSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/GrenadeConfig.hpp"
#include "ecs/components/GrenadeState.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponSpawner.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/DroppedWeaponSystem.hpp"
#include "ecs/systems/PickupGeometry.hpp"
#include "entt/entity/entity.hpp"

#include <cmath>
#include <vector>

namespace systems
{

/// @brief Check if any player overlaps the spawner and transfer the weapon on pickup.
/// @param registry      The ECS registry.
/// @param spawnerPos    Position of the spawner entity.
/// @param spawnerShape  Collision shape of the spawner.
/// @param spawner       Spawner component (weapon type, availability, cooldown).
inline void checkForPlayers(Registry& registry,
                            Position spawnerPos,
                            CollisionShape spawnerShape,
                            WeaponSpawner& spawner,
                            std::vector<PendingWeaponDrop>& pendingDrops)
{
    auto view =
        registry.view<Player, Position, CollisionShape, InputSnapshot, WeaponState, PlayerVisState, GrenadeState>();
    view.each([&](entt::entity /*player*/,
                  const Position& pos,
                  const CollisionShape& shape,
                  const InputSnapshot& input,
                  WeaponState& weapon,
                  const PlayerVisState& pvis,
                  GrenadeState& grenade) {
        if (overlapsAABB(spawnerPos.value, spawnerShape.halfExtents, pos.value, shape.halfExtents) && spawner.hasWeapon)
        {
            // Check if grenade spawner
            if (std::ranges::contains(kGrenadeTypes, spawner.type)) {
                int& currentAmmo = grenadeAmmo(grenade, spawner.type);
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
                currentAmmo += 2;
                return;
            }

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
            const WeaponConfig& config = getWeaponConfig(spawner.type);
            GunInstance& primary = getSlot(weapon, WeaponSlot::PRIMARY);
            GunInstance& secondary = getSlot(weapon, WeaponSlot::SECONDARY);

            // Never hold two of the same gun: if either slot already has this
            // type, top up that slot instead of placing a duplicate.
            if (primary.type == spawner.type) {
                primary.totalAmmo = config.defaultAmmoCapacity;
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
                return;
            }
            if (secondary.type == spawner.type) {
                secondary.totalAmmo = config.defaultAmmoCapacity;
                spawner.hasWeapon = false;
                spawner.spawnCooldown = weaponCooldownTime;
                return;
            }

            // Resolve the destination slot under the type-compatibility guard.
            // The currently-equipped slot is preferred; otherwise fall back to
            // PRIMARY. Grenades are not weapon-slot pickups; they live in
            // GrenadeState.
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
            GunInstance& slot = getSlot(weapon, targetSlot);
            // Drop the gun currently in the slot so the player keeps it in the
            // world rather than silently losing it. Toss to the player's side
            // with a brief pickup-immunity so it isn't instantly re-grabbed.
            const glm::vec3 rightAxis{std::cos(input.yaw), 0.0f, -std::sin(input.yaw)};
            const glm::vec3 dropFrom = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.4f * spawnEyeDir, 0.0f};
            pendingDrops.push_back(PendingWeaponDrop{
                .pos = dropFrom,
                .vel = rightAxis * 180.0f + glm::vec3{0.0f, 120.0f * spawnEyeDir, 0.0f},
                .gun = slot,
                .pickupDelay = k_swapDropPickupDelay,
            });
            slot = GunInstance{
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
    // Collect swap-out drops during iteration and spawn them afterward, so
    // creating drop entities doesn't invalidate the spawner view.
    std::vector<PendingWeaponDrop> pendingDrops;
    auto view = registry.view<WeaponSpawner, Position, CollisionShape>();
    view.each([&](WeaponSpawner& spawner, const Position& pos, const CollisionShape& shape) {
        checkForPlayers(registry, pos, shape, spawner, pendingDrops);
        if ((spawner.spawnCooldown - dt) > 0.0f) {
            spawner.spawnCooldown -= dt;
        } else {
            spawner.spawnCooldown = 0;
            spawner.hasWeapon = true;
        }
    });
    for (const PendingWeaponDrop& d : pendingDrops) {
        spawnDroppedWeapon(registry, d.pos, d.vel, d.gun, d.pickupDelay);
    }
}
} // namespace systems
