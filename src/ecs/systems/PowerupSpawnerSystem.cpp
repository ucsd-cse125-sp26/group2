/// @file PowerupSpawnerSystem.cpp
/// @brief Powerup spawner manager system.

#include "PowerupSpawnerSystem.hpp"

#include "PlayerStatusSystem.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PowerupSpawner.hpp"
#include "ecs/components/PowerupState.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PickupGeometry.hpp"

#include <algorithm>

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

void applyPowerupSpawnerConfig(Registry& registry, const MatchConfig& matchConfig)
{
    auto view = registry.view<PowerupSpawner>();
    view.each([&](PowerupSpawner& spawner) {
        if (spawner.hasPowerup)
            return;

        const float maxCooldown = spawner.hasSpawnedOnce ? matchConfig.powerupRespawnCooldownSeconds
                                                         : matchConfig.powerupInitialSpawnDelaySeconds;
        spawner.spawnCooldown = std::min(spawner.spawnCooldown, maxCooldown);
    });
}

void resetPowerupSpawnersForMatch(Registry& registry, const MatchConfig& matchConfig)
{
    auto view = registry.view<PowerupSpawner>();
    view.each([&](PowerupSpawner& spawner) {
        spawner.hasPowerup = false;
        spawner.hasSpawnedOnce = false;
        spawner.spawnCooldown = matchConfig.powerupInitialSpawnDelaySeconds;
    });
}

/// @brief Check if any player overlaps the spawner and transfer the powerup on collision.
/// @param registry      The ECS registry.
/// @param spawnerPos    Position of the spawner entity.
/// @param spawnerShape  Collision shape of the spawner.
/// @param spawner       Spawner component.
inline void checkForPlayers(Registry& registry,
                            Position spawnerPos,
                            CollisionShape spawnerShape,
                            PowerupSpawner& spawner,
                            const MatchConfig& matchConfig,
                            std::vector<NetParticleEvent>& outEvents)
{
    auto view = registry.view<Player, Position, CollisionShape, PowerupState>();
    view.each([&](entt::entity player, const Position& pos, const CollisionShape& shape, PowerupState& powerups) {
        if (overlapsAABB(spawnerPos.value, spawnerShape.halfExtents, pos.value, shape.halfExtents) &&
            spawner.hasPowerup)
        {
            const PowerupConfig config = getPowerupConfig(spawner.type);
            addOrRefreshPowerup(powerups, spawner.type, config.duration);
            spawner.hasPowerup = false;
            spawner.spawnCooldown = matchConfig.powerupRespawnCooldownSeconds;

            outEvents.push_back(NetParticleEvent{
                .source = player,
                .effectType = ParticleEffectType::PowerupPickup,
                .powerupType = spawner.type,
                .pos1 = spawnerPos.value,
            });

            if (spawner.type == PowerupType::Shield) {
                Health& healthComp = registry.get<Health>(player);
                healthComp.overShield = overShieldMax;
            }
        }
    });
}

void runPowerupSpawners(Registry& registry,
                        float dt,
                        const MatchConfig& matchConfig,
                        std::vector<NetParticleEvent>& outEvents)
{
    auto view = registry.view<PowerupSpawner, Position, CollisionShape>();
    view.each([&](PowerupSpawner& spawner, const Position& pos, const CollisionShape& shape) {
        checkForPlayers(registry, pos, shape, spawner, matchConfig, outEvents);
        if ((spawner.spawnCooldown - dt) > 0.0f) {
            spawner.spawnCooldown -= dt;
        } else {
            spawner.spawnCooldown = 0;
            spawner.hasPowerup = true;
            spawner.hasSpawnedOnce = true;
        }
    });
}
} // namespace systems
