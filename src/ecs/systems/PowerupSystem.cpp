/// @file PowerupSystem.cpp
/// @brief Powerup state manager system.

#include "PlayerStatusSystem.hpp"
#include "PowerupSpawnerSystem.hpp"
#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/BeamState.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/GrenadeConfig.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"
#include "ecs/components/HitboxHistory.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LagCompTarget.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PowerupState.hpp"
#include "ecs/components/RespawnTimer.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/components/WeaponConfig.hpp"
#include "ecs/components/WeaponState.hpp"
#include "ecs/physics/Raycast.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/LagCompensation.hpp"
#include "ecs/systems/WeaponSystem.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

using physics::HitboxHit;
using physics::HitscanHit;
using physics::resolveHitscan;
using physics::resolveHitscanHitbox;

namespace systems
{

void runPowerups(Registry& registry, float dt)
{
    auto view = registry.view<Player, PowerupState>();
    view.each([&](PowerupState& powerups) {

        for (ActivePowerup powerup : powerups.active) {
            powerup.timeRemaining -= dt;
            if (powerup.timeRemaining <= 0) {
                removePowerup(powerups, powerup.type);
            }
        }
    });
}

} // namespace systems
