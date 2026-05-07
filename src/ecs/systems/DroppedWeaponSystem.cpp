/// @file DroppedWeaponSystem.cpp
/// @brief Update system for player-dropped weapon pickups.

#include "DroppedWeaponSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/DroppedWeapon.hpp"
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

        // Overlap-grab refill: walking through a dropped weapon of a type the
        // player already holds tops that slot up using the snapshot ammo.
        if (overlapsAABB(dropPos.value, dropShape.halfExtents, pos.value, shape.halfExtents)) {
            if (weapon.primary.type == dw.type) {
                weapon.primary.totalAmmo = dw.totalAmmo;
                weapon.primary.currentMagAmmo = dw.currentMagAmmo;
                consumed = true;
                return;
            }
            if (weapon.secondary.type == dw.type) {
                weapon.secondary.totalAmmo = dw.totalAmmo;
                weapon.secondary.currentMagAmmo = dw.currentMagAmmo;
                consumed = true;
                return;
            }
        }

        // Look-and-press pickup: replaces the currently equipped slot.
        const float eyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.77f * eyeDir, 0.0f};
        const glm::vec3 viewFwd = viewForward(input.yaw, input.pitch);

        if (input.pickup && isPlayerLookingAtPickup(eye, viewFwd, dropPos.value)) {
            GunInstance picked{
                .type = dw.type,
                .totalAmmo = dw.totalAmmo,
                .currentMagAmmo = dw.currentMagAmmo,
                .fireCooldown = 0.0f,
            };
            if (weapon.current == WeaponSlot::PRIMARY) {
                weapon.primary = picked;
            } else {
                weapon.secondary = picked;
            }
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
