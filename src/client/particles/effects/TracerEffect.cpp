/// @file TracerEffect.cpp
/// @brief Implementation of oriented-capsule tracer effect for projectiles.

#include "TracerEffect.hpp"

#include "ecs/components/Position.hpp"
#include "ecs/components/TracerEmitter.hpp"
#include "ecs/components/Velocity.hpp"

void TracerEffect::attach(entt::entity e, Registry& registry)
{
    if (!registry.valid(e))
        return;
    if (!registry.all_of<Position, TracerEmitter>(e))
        return;

    auto* slot = pool_.spawn();
    if (!slot)
        return;

    const auto& pos = registry.get<Position>(e);
    const auto& emitter = registry.get<TracerEmitter>(e);

    slot->tip = pos.value;
    slot->tail = pos.value; // will be set on first update
    slot->radius = emitter.radius;
    slot->brightness = 1.f;
    slot->coreColor = emitter.coreColor;
    slot->edgeColor = emitter.edgeColor;
    slot->lifetime = 9999.f; // alive as long as entity is alive

    const uint32_t idx = pool_.liveCount() - 1;
    entityToIdx_[static_cast<uint32_t>(e)] = idx;
}

void TracerEffect::spawnFree(glm::vec3 tip, glm::vec3 tail, float lifetime)
{
    auto* slot = pool_.spawn();
    if (!slot)
        return;

    slot->tip = tip;
    slot->tail = tail;
    slot->radius = 0.6f;
    slot->brightness = 1.f;
    slot->coreColor = {1.f, 0.95f, 0.7f, 1.f};
    slot->edgeColor = {1.f, 0.40f, 0.05f, 0.f};
    slot->lifetime = lifetime; // < 9990 -> picked up by the decay loop
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
    }
    entityToIdx_.erase(it);
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

    // Decay fading tracers (detached from entity)
    // We iterate the pool directly; tracers with lifetime <= 9990 are fading
    for (uint32_t i = pool_.liveCount(); i-- > 0;) {
        auto* p = const_cast<TracerParticle*>(pool_.rawData()) + i;
        if (p->lifetime < 9990.f) {
            p->lifetime -= dt;
            p->brightness = std::max(0.f, p->lifetime / k_fadeTime);
            if (p->lifetime <= 0.f) {
                pool_.kill(i);
                // Rebuild entity->index map after kill
                for (auto& kv : entityToIdx_) {
                    if (kv.second == pool_.liveCount()) // the element that was swapped in
                        kv.second = i;
                }
            }
        }
    }
}
