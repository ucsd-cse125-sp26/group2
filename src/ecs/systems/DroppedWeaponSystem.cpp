/// @file DroppedWeaponSystem.cpp
/// @brief Update system for player-dropped weapon pickups.

#include "DroppedWeaponSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/DroppedWeapon.hpp"
#include "ecs/components/GrenadeConfig.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/systems/PickupGeometry.hpp"
#include "entt/entity/entity.hpp"

#include <vector>

namespace systems
{

/// @brief Try to grant the dropped weapon to a player.
/// @return True if the entity should be destroyed (pickup happened).
inline bool tryPickup(Registry& registry, Position dropPos, CollisionShape dropShape, const DroppedWeapon& dw)
{
    bool consumed = false;
    auto view = registry.view<Player, Position, CollisionShape, InputSnapshot, WeaponState, PlayerVisState>();
    view.each([&](entt::entity /*player*/,
                  const Position& pos,
                  const CollisionShape& shape,
                  const InputSnapshot& input,
                  WeaponState& weapon,
                  const PlayerVisState& pvis) {
        if (consumed)
            return;
        if (pvis.isDead) // dead players still have Position+WeaponState; don't let
            return;      // them self-consume drops created at their death point.

        // Overlap-grab refill: walking through a dropped weapon of a type the
        // player already holds tops that slot up using the snapshot ammo.
        if (overlapsAABB(dropPos.value, dropShape.halfExtents, pos.value, shape.halfExtents)) {
            GunInstance& primary = getSlot(weapon, WeaponSlot::PRIMARY);
            GunInstance& secondary = getSlot(weapon, WeaponSlot::SECONDARY);
            if (primary.type == dw.type) {
                primary.totalAmmo = dw.totalAmmo;
                primary.currentMagAmmo = dw.currentMagAmmo;
                consumed = true;
                return;
            }
            if (secondary.type == dw.type) {
                secondary.totalAmmo = dw.totalAmmo;
                secondary.currentMagAmmo = dw.currentMagAmmo;
                consumed = true;
                return;
            }
        }

        // Look-and-press pickup: replaces the currently equipped slot when it
        // can accept the dropped type, else falls back to PRIMARY. Mirrors the
        // type-guard policy in WeaponSpawnerSystem so a player holding GRENADE
        // can still pick up gun drops without dumping them into the wrong slot.
        const float eyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.77f * eyeDir, 0.0f};
        const glm::vec3 viewFwd = viewForward(input.yaw, input.pitch);

        if (input.pickup && isPlayerLookingAtPickup(eye, viewFwd, dropPos.value)) {
            const WeaponSlot targetSlot = canAcceptType(weapon.current, dw.type) ? weapon.current : WeaponSlot::PRIMARY;
            if (!canAcceptType(targetSlot, dw.type))
                return;
            getSlot(weapon, targetSlot) = GunInstance{
                .type = dw.type,
                .totalAmmo = dw.totalAmmo,
                .currentMagAmmo = dw.currentMagAmmo,
                .fireCooldown = 0.0f,
            };
            consumed = true;
        }
    });
    return consumed;
}

void runDroppedWeapons(Registry& registry, float dt)
{
    // Two-phase: collect entities to destroy, then destroy. Avoids
    // mutating the view's storage mid-iteration.
    std::vector<entt::entity> toDestroy;
    auto view = registry.view<DroppedWeapon, Position, CollisionShape>();
    view.each([&](entt::entity e, DroppedWeapon& dw, const Position& pos, const CollisionShape& shape) {
        if (tryPickup(registry, pos, shape, dw)) {
            toDestroy.push_back(e);
            return;
        }
        dw.despawnTimer -= dt;
        if (dw.despawnTimer <= 0.0f) {
            toDestroy.push_back(e);
        }
    });
    for (entt::entity e : toDestroy) {
        registry.destroy(e);
    }
}

} // namespace systems
