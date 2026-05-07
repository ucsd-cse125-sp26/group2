/// @file ExplosionSystem.cpp
/// @brief Explosion request processing and radial damage.

#include "ecs/systems/ExplosionSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Explosion.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/NetKillEvent.hpp"

#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <vector>

namespace systems
{

void queueExplosion(
    Registry& registry, glm::vec3 position, float radius, float maxDamage, entt::entity owner, float falloffExponent)
{
    const entt::entity explosion = registry.create();
    registry.emplace<Explosion>(explosion,
                                Explosion{.position = position,
                                          .radius = radius,
                                          .maxDamage = maxDamage,
                                          .falloffExponent = falloffExponent,
                                          .owner = owner});
}

void runExplosion(Registry& registry,
                  std::vector<NetParticleEvent>& outParticles,
                  std::vector<NetKillEvent>& killEvents)
{
    std::vector<entt::entity> resolvedExplosions;

    registry.view<Explosion>().each([&](entt::entity explosionEntity, const Explosion& explosion) {
        NetParticleEvent event;
        event.source = explosion.owner;
        event.effectType = ParticleEffectType::Explosion;
        event.pos1 = explosion.position;
        event.param = explosion.radius;
        outParticles.push_back(event);

        auto players = registry.view<Player, Position, CollisionShape>();
        for (const entt::entity player : players) {
            const Position& position = players.get<Position>(player);
            const CollisionShape& shape = players.get<CollisionShape>(player);

            const glm::vec3 closestPoint =
                glm::clamp(explosion.position, position.value - shape.halfExtents, position.value + shape.halfExtents);
            const glm::vec3 offset = closestPoint - explosion.position;
            const float distance = glm::length(offset);
            if (distance >= explosion.radius) {
                continue;
            }

            // Falloff: damage = maxDamage * (1 - d/r)^exponent.
            // Exponent 1.0 = linear; higher exponents give sharper falloff so direct hits
            // remain lethal while near misses do little damage.
            const float falloff = 1.0f - (distance / explosion.radius);
            const float damage = explosion.maxDamage * std::pow(falloff, explosion.falloffExponent);
            if (damage <= 0.0f) {
                continue;
            }

            entt::entity killer = explosion.owner;
            if (killer == entt::null || !registry.valid(killer)) {
                killer = player;
            }
            applyDamage(damage, player, killer, registry, killEvents);
        }

        resolvedExplosions.push_back(explosionEntity);
    });

    for (const entt::entity explosionEntity : resolvedExplosions) {
        if (registry.valid(explosionEntity)) {
            registry.destroy(explosionEntity);
        }
    }
}

} // namespace systems
