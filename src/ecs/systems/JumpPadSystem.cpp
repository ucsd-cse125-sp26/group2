/// @file JumpPadSystem.cpp
/// @brief Jump pad trigger system.

#include "JumpPadSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/JumpPad.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/systems/PickupGeometry.hpp"

namespace systems
{

void runJumpPads(Registry& registry, float /*dt*/)
{
    auto padView = registry.view<JumpPad, Position, CollisionShape>();
    padView.each([&](const JumpPad& pad, const Position& padPos, const CollisionShape& padShape) {
        auto players = registry.view<Player, Position, CollisionShape, Velocity>();
        players.each([&](entt::entity /*player*/,
                         const Position& pos,
                         const CollisionShape& shape,
                         Velocity& vel) {
            if (!overlapsAABB(padPos.value, padShape.halfExtents, pos.value, shape.halfExtents))
                return;

            // Replace vertical velocity so a falling player still gets the
            // full launch height; add horizontal so a directional pad can
            // sling the player sideways without negating existing momentum.
            // Re-trigger is implicitly bounded: the new Y velocity lifts the
            // player out of the pad AABB within one or two ticks.
            vel.value.x += pad.velocity.x;
            vel.value.y = pad.velocity.y;
            vel.value.z += pad.velocity.z;
        });
    });
}

} // namespace systems
