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

/// @brief Create an explosion entity at the given position.
///
/// The explosion is processed on the next call to runExplosion().
///
/// @param registry          The ECS registry.
/// @param position          World-space center of the explosion.
/// @param radius            Blast radius (damage falls off to zero at edge).
/// @param maxDamage         Maximum damage at the epicenter.
/// @param owner             Entity that caused the explosion (for self-damage / kill credit).
/// @param falloffExponent   Damage curve exponent (1 = linear, 3 = cubic/sharp). Defaults to linear.
void queueExplosion(Registry& registry,
                    glm::vec3 position,
                    float radius,
                    float maxDamage,
                    entt::entity owner,
                    float falloffExponent = 1.0f);

/// @brief Process all pending explosions: apply radial damage and emit particle events.
///
/// For each Explosion component, damages every player within the blast radius
/// (`damage = maxDamage * pow(1 - d/r, falloffExponent)`), emits a
/// ParticleEffectType::Explosion event, and destroys the explosion entity.
///
/// @param registry      The ECS registry.
/// @param outParticles  Accumulates particle events for network broadcast.
/// @param killEvents    Accumulates kill events for network broadcast.
void runExplosion(Registry& registry,
                  std::vector<NetParticleEvent>& outParticles,
                  std::vector<NetKillEvent>& killEvents);

} // namespace systems
