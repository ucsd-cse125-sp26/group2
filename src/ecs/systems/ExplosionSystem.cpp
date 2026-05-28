/// @file ExplosionSystem.cpp
/// @brief Explosion request processing and radial damage.

#include "ecs/systems/ExplosionSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/Explosion.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Forces.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/NetKillEvent.hpp"

#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <vector>

namespace systems
{

void queueExplosion(Registry& registry,
                    glm::vec3 position,
                    float radius,
                    float maxDamage,
                    entt::entity owner,
                    float falloffExponent,
                    float selfDamageMultiplier,
                    float maxKnockback,
                    float knockbackFalloffExponent,
                    entt::entity directKillTarget)
{
    const entt::entity explosion = registry.create();
    registry.emplace<Explosion>(explosion,
                                Explosion{.position = position,
                                          .radius = radius,
                                          .maxDamage = maxDamage,
                                          .falloffExponent = falloffExponent,
                                          .selfDamageMultiplier = selfDamageMultiplier,
                                          .maxKnockback = maxKnockback,
                                          .knockbackFalloffExponent = knockbackFalloffExponent,
                                          .owner = owner,
                                          .directKillTarget = directKillTarget});
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

        // Guaranteed kill for a grenade stuck directly to a player: bypass the
        // radial falloff so a stuck grenade is always lethal on its host. The
        // radial pass below still applies normal AoE to everyone else (and the
        // host's own radial damage is harmless — applyDamage ignores the dead).
        if (registry.valid(explosion.directKillTarget)) {
            entt::entity killer = explosion.owner;
            if (killer == entt::null || !registry.valid(killer)) {
                killer = explosion.directKillTarget;
            }
            applyDamage(1.0e6f, explosion.directKillTarget, killer, registry, killEvents);
        }

        auto players = registry.view<Player, Position, CollisionShape>();
        for (const entt::entity player : players) {
            Position& position = players.get<Position>(player);
            const CollisionShape& shape = players.get<CollisionShape>(player);

            const glm::vec3 closestPoint =
                glm::clamp(explosion.position, position.value - shape.halfExtents, position.value + shape.halfExtents);
            const glm::vec3 offset = closestPoint - explosion.position;
            const float distance = glm::length(offset);
            if (distance >= explosion.radius) {
                continue;
            }

            // Falloff: value = peakValue * (1 - d/r)^exponent.
            // Exponent 1.0 = linear; higher exponents give sharper falloff so direct hits
            // remain lethal while near misses do little damage.
            const float falloff = 1.0f - (distance / explosion.radius);

            // ── Damage ──────────────────────────────────────────────────────
            float damage = explosion.maxDamage * std::pow(falloff, explosion.falloffExponent);
            if (player == explosion.owner) {
                damage *= explosion.selfDamageMultiplier;
            }
            if (damage > 0.0f) {
                entt::entity killer = explosion.owner;
                if (killer == entt::null || !registry.valid(killer)) {
                    killer = player;
                }
                applyDamage(damage, player, killer, registry, killEvents);
            }

            // ── Knockback (rocket-jump impulse) ─────────────────────────────
            // Push direction is center-to-center, not closest-point-based — keeps a feet-
            // exploding rocket pushing UP rather than sideways toward the closest AABB face.
            // Falls back to world-up if explosion is exactly at the player's center.
            if (explosion.maxKnockback > 0.0f) {
                const float falloffScale = std::pow(falloff, explosion.knockbackFalloffExponent);
                const float knockMag = explosion.maxKnockback * falloffScale;
                if (knockMag > 0.0f) {
                    const glm::vec3 toPlayer = position.value - explosion.position;
                    const float toPlayerLen = glm::length(toPlayer);
                    const glm::vec3 dir =
                        (toPlayerLen > 1e-4f) ? (toPlayer / toPlayerLen) : glm::vec3{0.0f, 1.0f, 0.0f};
                    // Phase 6: route knockback through the unified impulse API so
                    // it composes correctly with future mass-aware bodies.  For
                    // entities without RigidBody (today's players) this falls
                    // back to direct `velocity += impulse` — identical to the
                    // old code path.
                    physics::forces::applyImpulse(registry, player, dir * knockMag);

                    // Grounded launch: a standing victim barely reacts to the impulse
                    // above — ground friction eats the horizontal part next tick and a
                    // foot-level blast is nearly horizontal. Lift them off the floor,
                    // floor their vertical speed, and clear `grounded` so next tick's
                    // friction branch is skipped (the same momentum-preserving trick a
                    // ground jump uses).
                    auto* vis = registry.try_get<PlayerVisState>(player);
                    if (vis != nullptr && vis->grounded) {
                        const float upSign = vis->gravityFlipped ? -1.0f : 1.0f;
                        position.value.y += upSign * physics::k_explosionGroundPopOffset * falloffScale;

                        if (auto* vel = registry.try_get<Velocity>(player)) {
                            const float verticalBoost = physics::k_explosionGroundVerticalBoost * falloffScale;
                            if (vel->value.y * upSign < verticalBoost)
                                vel->value.y = upSign * verticalBoost;
                        }
                        vis->grounded = false;
                    }
                }
            }
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
