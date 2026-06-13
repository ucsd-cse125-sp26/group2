/// @file FireSystem.hpp
/// @brief Tick all active FireField entities: apply DoT damage and expire.

#pragma once

#include "ecs/registry/Registry.hpp"
#include "ecs/components/WeaponState.hpp"
#include "network/NetKillEvent.hpp"

#include <glm/vec3.hpp>
#include <vector>

namespace systems
{

/// @brief Spawn a FireField at `position` that lasts `duration` seconds and
/// deals `dps` damage to players inside `radius`. `owner` gets kill credit.
void spawnFireField(Registry& registry,
                    glm::vec3 position,
                    float radius,
                    float duration,
                    float dps,
                    entt::entity owner,
                    WeaponType weaponType = WeaponType::Molotov);

/// @brief Tick all FireField entities: decrement `remaining`, apply DoT damage
/// at fixed sub-intervals (4 Hz), and destroy expired fields.
///
/// Self-damage is scaled by 0.4 (matches rocket/grenade self-damage philosophy).
void runFireField(Registry& registry, float dt, std::vector<NetKillEvent>& killEvents);

} // namespace systems
