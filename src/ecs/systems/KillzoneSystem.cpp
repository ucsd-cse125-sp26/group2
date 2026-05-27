/// @file KillzoneSystem.cpp
/// @brief Killzone trigger system.

#include "KillzoneSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Killzone.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/systems/PickupGeometry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"

namespace systems
{

void runKillzones(Registry& registry, std::vector<NetKillEvent>& killEvents)
{
    auto zoneView = registry.view<Killzone, Position, CollisionShape>();
    zoneView.each([&](const Killzone& zone, const Position& zonePos, const CollisionShape& zoneShape) {
        auto players = registry.view<Player, Position, CollisionShape>();
        players.each(
            [&](entt::entity player, const Position& pos, const CollisionShape& shape) {
                if (!overlapsAABB(zonePos.value, zoneShape.halfExtents, pos.value, shape.halfExtents))
                    return;

                // Self-credit: the killzone has no owning player, and a
                // null killer breaks the kill feed serialiser. Crediting
                // the victim mirrors how the player would be credited
                // for walking off a ledge.
                entt::entity killer = player;
                applyDamage(zone.damage, player, killer, registry, killEvents);
            });
    });
}

} // namespace systems
