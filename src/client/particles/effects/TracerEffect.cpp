/// @file TracerEffect.cpp
/// @brief Implementation of oriented-capsule tracer effect for projectiles.

#include "TracerEffect.hpp"

#include "ecs/components/Position.hpp"
#include "ecs/components/TracerEmitter.hpp"
#include "ecs/components/Velocity.hpp"

#include <algorithm>
#include <glm/geometric.hpp>

namespace
{
glm::vec3 normalizedOrZero(glm::vec3 v)
{
    const float len = glm::length(v);
    if (len <= 0.0001f)
        return {};
    return v / len;
}
} // namespace

void TracerEffect::attach(entt::entity e, Registry& registry)
{
    if (!registry.valid(e))
        return;
    if (!registry.all_of<Position, TracerEmitter>(e))
        return;
    if (entityToIdx_.contains(static_cast<uint32_t>(e)))
        return;

    auto* slot = pool_.spawn();
    if (!slot)
        return;
    const uint32_t idx = pool_.liveCount() - 1;

    const auto& pos = registry.get<Position>(e);
    const auto& emitter = registry.get<TracerEmitter>(e);

    slot->tip = pos.value;
    slot->tail = pos.value; // will be set on first update
    slot->radius = emitter.radius;
    slot->brightness = 1.f;
    slot->coreColor = emitter.coreColor;
    slot->edgeColor = emitter.edgeColor;
    slot->lifetime = 9999.f; // alive as long as entity is alive

    runtime_[idx] = RuntimeState{.kind = RuntimeKind::Entity, .entity = e};
    entityToIdx_[static_cast<uint32_t>(e)] = idx;
}

void TracerEffect::spawnRifleTracer(glm::vec3 origin, glm::vec3 dir, float range)
{
    const glm::vec3 unitDir = normalizedOrZero(dir);
    if (unitDir == glm::vec3{})
        return;

    const float distance = std::max(1.0f, range);
    auto* slot = pool_.spawn();
    if (!slot)
        return;
    const uint32_t idx = pool_.liveCount() - 1;

    const float initialTravel = std::min(k_rifleSpawnLead, distance);
    slot->tip = origin + unitDir * initialTravel;
    slot->tail = origin;
    slot->radius = k_rifleRadius;
    slot->brightness = 1.f;
    slot->coreColor = {1.0f, 0.88f, 0.48f, 1.0f};
    slot->edgeColor = {1.0f, 0.38f, 0.08f, 0.36f};
    slot->lifetime = (distance + k_rifleTrailLength - initialTravel) / k_rifleVisualSpeed;

    runtime_[idx] = RuntimeState{
        .kind = RuntimeKind::RifleProjectile,
        .origin = origin,
        .dir = unitDir,
        .distance = distance,
        .speed = k_rifleVisualSpeed,
        .trailLength = k_rifleTrailLength,
    };
}

void TracerEffect::detach(entt::entity e)
{
    auto it = entityToIdx_.find(static_cast<uint32_t>(e));
    if (it == entityToIdx_.end())
        return;
    // Mark as fading -- lifetime will expire naturally
    if (it->second < pool_.liveCount()) {
        auto* p = const_cast<TracerParticle*>(pool_.rawData()) + it->second;
        p->lifetime = k_fadeTime;
        runtime_[it->second] = {};
    }
    entityToIdx_.erase(it);
}

void TracerEffect::killTracer(uint32_t idx)
{
    if (idx >= pool_.liveCount())
        return;

    const uint32_t last = pool_.liveCount() - 1;
    const RuntimeState moved = runtime_[last];
    pool_.kill(idx);

    if (idx != last) {
        runtime_[idx] = moved;
        if (moved.kind == RuntimeKind::Entity && moved.entity != entt::null)
            entityToIdx_[static_cast<uint32_t>(moved.entity)] = idx;
    }
    runtime_[last] = {};
}

void TracerEffect::updateRifleTracer(uint32_t idx, float dt)
{
    RuntimeState& state = runtime_[idx];
    TracerParticle* p = const_cast<TracerParticle*>(pool_.rawData()) + idx;

    state.age += std::max(0.0f, dt);
    const float travel = k_rifleSpawnLead + state.age * state.speed;
    const float retireTravel = state.distance + state.trailLength;
    if (travel >= retireTravel) {
        killTracer(idx);
        return;
    }

    const float tipDist = std::min(travel, state.distance);
    const float tailDist = std::clamp(travel - state.trailLength, 0.0f, state.distance);
    p->tip = state.origin + state.dir * tipDist;
    p->tail = state.origin + state.dir * tailDist;

    const float impactFade =
        (travel <= state.distance) ? 1.0f : std::clamp((retireTravel - travel) / state.trailLength, 0.0f, 1.0f);
    p->brightness = impactFade;
    p->lifetime = std::max(0.0f, (retireTravel - travel) / state.speed);
}

void TracerEffect::update(float dt, Registry& registry)
{
    // Update tracers that still have a live entity
    registry.view<Position, Velocity, TracerEmitter>().each(
        [&](entt::entity e, const Position& pos, const Velocity& vel, TracerEmitter& emitter) {
            auto it = entityToIdx_.find(static_cast<uint32_t>(e));
            if (it == entityToIdx_.end()) {
                // Newly spawned entity not yet tracked
                attach(e, registry);
                emitter.prevPos = pos.value;
                return;
            }

            const uint32_t idx = it->second;
            if (idx >= pool_.liveCount())
                return;

            // We need mutable access -- rawData returns const*, so we cast.
            // The pool_ owns the data array so this is safe.
            auto* p = const_cast<TracerParticle*>(pool_.rawData()) + idx;

            p->tip = pos.value;

            const float speed = glm::length(vel.value);
            if (speed > 0.001f)
                p->tail = p->tip - glm::normalize(vel.value) * k_streakLength;
            else
                p->tail = emitter.prevPos;

            p->lifetime = 9999.f; // still alive
            p->brightness = 1.f;
            emitter.prevPos = pos.value;
        });

    for (uint32_t i = pool_.liveCount(); i-- > 0;) {
        auto* p = const_cast<TracerParticle*>(pool_.rawData()) + i;

        if (runtime_[i].kind == RuntimeKind::RifleProjectile) {
            updateRifleTracer(i, dt);
            continue;
        }

        // Decay fading tracers detached from an entity.
        if (runtime_[i].kind == RuntimeKind::None && p->lifetime < 9990.f) {
            p->lifetime -= dt;
            p->brightness = std::max(0.f, p->lifetime / k_fadeTime);
            if (p->lifetime <= 0.f)
                killTracer(i);
        }
    }
}
