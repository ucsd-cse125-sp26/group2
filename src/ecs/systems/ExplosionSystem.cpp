/// @file ExplosionSystem.cpp
/// @brief Explosion request processing and radial damage.

#include "ecs/systems/ExplosionSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Explosion.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <vector>

namespace systems
{

void queueExplosion(Registry& registry, glm::vec3 position, float radius, float maxDamage, entt::entity owner)
{
    const entt::entity explosion = registry.create();
    registry.emplace<Explosion>(
        explosion, Explosion{.position = position, .radius = radius, .maxDamage = maxDamage, .owner = owner});
}

void runExplosion(Registry& registry, std::vector<NetParticleEvent>& outParticles)
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

            const glm::vec3 closestPoint = glm::clamp(
                explosion.position, position.value - shape.halfExtents, position.value + shape.halfExtents);
            const glm::vec3 offset = closestPoint - explosion.position;
            const float distance = glm::length(offset);
            if (distance >= explosion.radius) {
                continue;
            }

            const float damage = explosion.maxDamage * (1.0f - (distance / explosion.radius));
            if (damage <= 0.0f) {
                continue;
            }

            entt::entity killer = explosion.owner;
            if (killer == entt::null || !registry.valid(killer)) {
                killer = player;
            }
            applyDamage(damage, player, killer, registry);
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
