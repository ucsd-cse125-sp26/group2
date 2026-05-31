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
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/systems/PickupGeometry.hpp"
#include "entt/entity/entity.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace systems
{

void spawnDroppedWeapon(Registry& registry,
                        glm::vec3 pos,
                        glm::vec3 initialVel,
                        const GunInstance& gun,
                        float pickupDelay)
{
    const entt::entity e = registry.create();
    registry.emplace<Position>(e, pos);
    // Compact weapon-sized AABB so the model rests near the floor under
    // gravity; the pickup overlap test still gets a generous catch radius
    // once the player's own AABB is added in.
    CollisionShape dropShape{};
    dropShape.halfExtents = glm::vec3{12.0f, 6.0f, 12.0f};
    registry.emplace<CollisionShape>(e, dropShape);
    // Velocity + RigidBody so DynamicsSystem ticks it with gravity and resolves
    // it against the world. linearDamping bleeds momentum so it doesn't skid.
    registry.emplace<Velocity>(e, Velocity{initialVel});
    RigidBody rb{};
    rb.linearDamping = 2.0f;
    rb.angularDamping = 4.0f;
    registry.emplace<RigidBody>(e, rb);
    registry.emplace<DroppedWeapon>(e,
                                    DroppedWeapon{
                                        .type = gun.type,
                                        .totalAmmo = gun.totalAmmo,
                                        .currentMagAmmo = gun.currentMagAmmo,
                                        .despawnTimer = k_droppedWeaponLifetime,
                                        .pickupDelay = pickupDelay,
                                    });
}

/// @brief Try to grant the dropped weapon to a player.
/// @param pendingDrops  Collects any swap-out gun to re-drop (spawned after
///                      iteration to avoid invalidating the active view).
/// @return True if the entity should be destroyed (pickup happened).
inline bool tryPickup(Registry& registry,
                      Position dropPos,
                      CollisionShape dropShape,
                      const DroppedWeapon& dw,
                      std::vector<PendingWeaponDrop>& pendingDrops)
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

        GunInstance& primary = getSlot(weapon, WeaponSlot::PRIMARY);
        GunInstance& secondary = getSlot(weapon, WeaponSlot::SECONDARY);

        // Overlap-grab refill: walking through a dropped weapon of a type the
        // player already holds tops that slot up using the snapshot ammo. No
        // interaction needed, and never creates a duplicate.
        if (overlapsAABB(dropPos.value, dropShape.halfExtents, pos.value, shape.halfExtents)) {
            if (primary.type == dw.type) {
                primary.totalAmmo = dw.totalAmmo;
                consumed = true;
                return;
            }
            if (secondary.type == dw.type) {
                secondary.totalAmmo = dw.totalAmmo;
                consumed = true;
                return;
            }
        }

        // Look-and-press pickup: replaces the currently equipped slot when it
        // can accept the dropped type, else falls back to PRIMARY. Grenades
        // are equipment, not dropped weapon-slot pickups.
        const float eyeDir = pvis.gravityFlipped ? -1.0f : 1.0f;
        const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.77f * eyeDir, 0.0f};
        const glm::vec3 viewFwd = viewForward(input.yaw, input.pitch);

        if (input.pickup && isPlayerLookingAtPickup(eye, viewFwd, dropPos.value)) {
            // Never hold two of the same gun: if either slot already has this
            // type, top it up instead of placing a duplicate.
            if (primary.type == dw.type) {
                primary.totalAmmo = dw.totalAmmo;
                consumed = true;
                return;
            }
            if (secondary.type == dw.type) {
                secondary.totalAmmo = dw.totalAmmo;
                consumed = true;
                return;
            }

            const WeaponSlot targetSlot = canAcceptType(weapon.current, dw.type) ? weapon.current : WeaponSlot::PRIMARY;
            if (!canAcceptType(targetSlot, dw.type))
                return;
            GunInstance& slot = getSlot(weapon, targetSlot);
            // Drop the gun currently in the slot so the player doesn't silently
            // lose it. Toss it to the player's side so it doesn't sit on top of
            // the new pickup, with a brief pickup-immunity so it isn't re-grabbed.
            const glm::vec3 rightAxis{std::cos(input.yaw), 0.0f, -std::sin(input.yaw)};
            const glm::vec3 dropFrom = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.4f * eyeDir, 0.0f};
            pendingDrops.push_back(PendingWeaponDrop{
                .pos = dropFrom,
                .vel = rightAxis * 180.0f + glm::vec3{0.0f, 120.0f * eyeDir, 0.0f},
                .gun = slot,
                .pickupDelay = k_swapDropPickupDelay,
            });
            slot = GunInstance{
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
    // Two-phase: collect entities to destroy and any swap-out drops, then apply
    // them. Avoids mutating the view's storage mid-iteration.
    std::vector<entt::entity> toDestroy;
    std::vector<PendingWeaponDrop> pendingDrops;
    auto view = registry.view<DroppedWeapon, Position, CollisionShape>();
    view.each([&](entt::entity e, DroppedWeapon& dw, const Position& pos, const CollisionShape& shape) {
        if (dw.pickupDelay > 0.0f) {
            dw.pickupDelay = std::max(0.0f, dw.pickupDelay - dt);
        } else if (tryPickup(registry, pos, shape, dw, pendingDrops)) {
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
    for (const PendingWeaponDrop& d : pendingDrops) {
        spawnDroppedWeapon(registry, d.pos, d.vel, d.gun, d.pickupDelay);
    }
}

} // namespace systems
