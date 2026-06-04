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
/// @param registry                 The ECS registry.
/// @param position                 World-space center of the explosion.
/// @param radius                   Blast radius (damage falls off to zero at edge).
/// @param maxDamage                Maximum damage at the epicenter.
/// @param owner                    Entity that caused the explosion (for self-damage / kill credit).
/// @param falloffExponent          Damage curve exponent (1 = linear, 3 = cubic/sharp).
/// @param selfDamageMultiplier     Damage scale when victim == owner (e.g. 0.4 for rocket jumps).
/// @param maxKnockback             Peak knockback velocity (u/s) imparted at the epicenter.
/// @param knockbackFalloffExponent Knockback curve exponent (same form as damage falloff).
/// @param directKillTarget         If valid, this entity takes guaranteed lethal damage regardless
///                                 of radius/falloff (used by a grenade stuck to a player).
void queueExplosion(Registry& registry,
                    glm::vec3 position,
                    float radius,
                    float maxDamage,
                    entt::entity owner,
                    float falloffExponent = 1.0f,
                    float selfDamageMultiplier = 1.0f,
                    float maxKnockback = 0.0f,
                    float knockbackFalloffExponent = 1.0f,
                    entt::entity directKillTarget = entt::null,
                    WeaponType weaponType = WeaponType::Rocket);

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
