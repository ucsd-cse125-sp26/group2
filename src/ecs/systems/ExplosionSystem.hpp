/// @file ExplosionSystem.hpp
/// @brief Explosion request processing and radial damage.

#pragma once

#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/NetKillEvent.hpp"
#include "network/ShotEvent.hpp"

#include <glm/vec3.hpp>
#include <vector>

namespace systems
{

void queueExplosion(Registry& registry, glm::vec3 position, float radius, float maxDamage, entt::entity owner);

void runExplosion(Registry& registry,
                  std::vector<NetParticleEvent>& outParticles,
                  std::vector<NetKillEvent>& killEvents);

} // namespace systems
