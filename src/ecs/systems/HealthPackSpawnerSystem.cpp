/// @file HealthPackSpawnerSystem.cpp
/// @brief Health pack spawner manager system.

#include "HealthPackSpawnerSystem.hpp"

#include "PlayerStatusSystem.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/HealthPackSpawner.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PickupGeometry.hpp"

namespace systems
{

/// @brief Check if any player overlaps the spawner and transfer the weapon on pickup.
/// @param registry      The ECS registry.
/// @param spawnerPos    Position of the spawner entity.
/// @param spawnerShape  Collision shape of the spawner.
/// @param spawner       Spawner component
inline void
checkForPlayers(Registry& registry, Position spawnerPos, CollisionShape spawnerShape, HealthPackSpawner& spawner)
{
    auto view = registry.view<Player, Position, CollisionShape, Health>();
    view.each([&](entt::entity /*player*/,
                  const Position& pos,
                  const CollisionShape& shape,
                  Health playerHealth) {
        if (overlapsAABB(spawnerPos.value, spawnerShape.halfExtents, pos.value, shape.halfExtents) && spawner.hasPack)
        {
            spawner.hasPack = false;
            spawner.spawnCooldown = healthPackCooldownTime;

            applyHeal(spawner.healAmount, playerHealth);
        }
    });
}

void runHealthPackSpawners(Registry& registry, float dt)
{
    auto view = registry.view<HealthPackSpawner, Position, CollisionShape>();
    view.each([&](HealthPackSpawner& spawner, const Position& pos, const CollisionShape& shape) {
        checkForPlayers(registry, pos, shape, spawner);
        if ((spawner.spawnCooldown - dt) > 0.0f) {
            spawner.spawnCooldown -= dt;
        } else {
            spawner.spawnCooldown = 0;
            spawner.hasPack = true;
        }
    });
}
} // namespace systems
