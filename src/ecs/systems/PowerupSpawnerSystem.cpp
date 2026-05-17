/// @file PowerupSpawnerSystem.cpp
/// @brief Powerup spawner manager system.

#include "PowerupSpawnerSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PowerupSpawner.hpp"
#include "ecs/components/PowerupState.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PickupGeometry.hpp"

namespace systems
{

bool hasPowerup(const PowerupState& state, PowerupType type)
{
    return std::ranges::find_if(state.active, [type](const ActivePowerup& powerup) { return powerup.type == type; }) !=
           state.active.end();
}

void addOrRefreshPowerup(PowerupState& state, PowerupType type, float duration)
{
    auto it = std::find_if(state.active.begin(), state.active.end(), [type](const ActivePowerup& powerup) {
        return powerup.type == type;
    });

    if (it != state.active.end()) {
        it->timeRemaining = duration;
        return;
    }

    state.active.push_back(ActivePowerup{
        .type = type,
        .timeRemaining = duration,
    });
}

void removePowerup(PowerupState& state, PowerupType type)
{
    std::erase_if(state.active, [type](const ActivePowerup& powerup) { return powerup.type == type; });
}

/// @brief Check if any player overlaps the spawner and transfer the powerup on collision.
/// @param registry      The ECS registry.
/// @param spawnerPos    Position of the spawner entity.
/// @param spawnerShape  Collision shape of the spawner.
/// @param spawner       Spawner component.
inline void
checkForPlayers(Registry& registry, Position spawnerPos, CollisionShape spawnerShape, PowerupSpawner& spawner)
{
    auto view = registry.view<Player, Position, CollisionShape, PowerupState>();
    view.each([&](const Position& pos, const CollisionShape& shape, PowerupState& powerups) {
        if (overlapsAABB(spawnerPos.value, spawnerShape.halfExtents, pos.value, shape.halfExtents) &&
            spawner.hasPowerup)
        {
            const PowerupConfig config = getPowerupConfig(spawner.type);
            addOrRefreshPowerup(powerups, spawner.type, config.duration);
            spawner.hasPowerup = false;
            spawner.spawnCooldown = config.spawnCooldown;
        }
    });
}

void runPowerupSpawners(Registry& registry, float dt)
{
    auto view = registry.view<PowerupSpawner, Position, CollisionShape>();
    view.each([&](PowerupSpawner& spawner, const Position& pos, const CollisionShape& shape) {
        checkForPlayers(registry, pos, shape, spawner);
        if ((spawner.spawnCooldown - dt) > 0.0f) {
            spawner.spawnCooldown -= dt;
        } else {
            spawner.spawnCooldown = 0;
            spawner.hasPowerup = true;
        }
    });
}
} // namespace systems
