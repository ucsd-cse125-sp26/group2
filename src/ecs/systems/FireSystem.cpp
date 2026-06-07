/// @file FireSystem.cpp
/// @brief Tick all active FireField entities: apply DoT damage and expire.

#include "ecs/systems/FireSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/FireField.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <vector>

namespace systems
{

namespace
{
constexpr float k_tickPeriod = 0.25f;     ///< Apply damage at 4 Hz to keep numbers stable across framerates.
constexpr float k_selfDamageScale = 0.4f; ///< Same self-damage philosophy as rockets.
} // namespace

void spawnFireField(Registry& registry,
                    glm::vec3 position,
                    float radius,
                    float duration,
                    float dps,
                    entt::entity owner,
                    WeaponType weaponType)
{
    const entt::entity field = registry.create();
    registry.emplace<FireField>(field,
                                FireField{.position = position,
                                          .radius = radius,
                                          .remaining = duration,
                                          .dps = dps,
                                          .tickAccumulator = 0.0f,
                                          .owner = owner,
                                          .weaponType = weaponType});
}

void runFireField(Registry& registry, float dt, std::vector<NetKillEvent>& killEvents)
{
    std::vector<entt::entity> expired;

    registry.view<FireField>().each([&](entt::entity fieldEntity, FireField& field) {
        field.remaining -= dt;
        field.tickAccumulator += dt;

        // Apply damage in fixed sub-intervals to keep DoT consistent across framerates.
        while (field.tickAccumulator >= k_tickPeriod) {
            field.tickAccumulator -= k_tickPeriod;
            const float k_tickDamage = field.dps * k_tickPeriod;

            auto players = registry.view<Player, Position, CollisionShape>();
            for (const entt::entity player : players) {
                const Position& pos = players.get<Position>(player);
                const CollisionShape& shape = players.get<CollisionShape>(player);

                // Closest-point AABB test (parity with ExplosionSystem).
                const glm::vec3 closest =
                    glm::clamp(field.position, pos.value - shape.halfExtents, pos.value + shape.halfExtents);
                const float distance = glm::length(closest - field.position);
                if (distance >= field.radius)
                    continue;

                float damage = k_tickDamage;
                if (player == field.owner) {
                    damage *= k_selfDamageScale;
                }
                if (damage <= 0.0f)
                    continue;

                entt::entity killer = field.owner;
                if (killer == entt::null || !registry.valid(killer)) {
                    killer = player;
                }
                applyDamage(damage,
                            player,
                            killer,
                            registry,
                            killEvents,
                            BodyRegion::UpperTorso,
                            static_cast<int>(field.weaponType));
            }
        }

        if (field.remaining <= 0.0f) {
            expired.push_back(fieldEntity);
        }
    });

    for (const entt::entity e : expired) {
        if (registry.valid(e)) {
            registry.destroy(e);
        }
    }
}

} // namespace systems
