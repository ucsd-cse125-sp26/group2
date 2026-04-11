/// @file SmokeEffect.cpp
/// @brief Implementation of volumetric smoke billboard particle effect.

#include "SmokeEffect.hpp"

#include "ecs/components/ParticleEmitterTag.hpp"
#include "ecs/components/Position.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

/// @brief Return a random float in [0, 1].
static float randf()
{
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

/// @brief Return a random float in [lo, hi].
/// @param lo Lower bound (inclusive).
/// @param hi Upper bound (inclusive).
/// @return Random value uniformly distributed between lo and hi.
static float randRange(float lo, float hi)
{
    return lo + randf() * (hi - lo);
}

void SmokeEffect::spawn(glm::vec3 pos, float radius, bool isFire)
{
    const int count = isFire ? 4 : 8;
    for (int i = 0; i < count; ++i) {
        auto* p = pool_.spawn();
        if (!p)
            break;

        const float angle = randf() * 2.f * glm::pi<float>();
        const float r = randf() * radius;
        p->pos = pos + glm::vec3{std::cos(angle) * r, randf() * 10.f, std::sin(angle) * r};
        p->size = randRange(25.f, 50.f);
        p->maxLifetime = randRange(3.f, 5.f);
        p->normalizedAge = 0.f;
        p->rotation = randRange(-0.3f, 0.3f);

        if (isFire)
            p->color = glm::vec4{0.9f, 0.4f, 0.05f, 0.f}; // fire orange, premul starts 0
        else
            p->color = glm::vec4{0.4f, 0.4f, 0.4f, 0.f};  // grey smoke
    }
}

void SmokeEffect::update(float dt, Registry& registry, glm::vec3 camPos, glm::vec3 camForward)
{
    // Drive continuous emitters
    registry.view<Position, ParticleEmitterTag>().each([&](const Position& pos, ParticleEmitterTag& tag) {
        tag.accumulator += dt;
        const float interval = 1.f / tag.ratePerSecond;
        while (tag.accumulator >= interval) {
            tag.accumulator -= interval;
            spawn(pos.value, tag.radius, tag.type == EmitterType::Fire);
        }
    });

    // Simulate
    pool_.update([&](SmokeParticle& p) -> bool {
        p.normalizedAge += dt / p.maxLifetime;
        if (p.normalizedAge >= 1.f)
            return false;

        p.pos.y += k_upDrift * dt;
        p.size += (120.f - p.size) * 0.3f * dt; // grow toward max
        p.rotation += dt * 0.15f;

        // Alpha: fade in 0->0.35 during first 20% of life, out 0.35->0 during last 30%
        float alpha;
        if (p.normalizedAge < 0.2f)
            alpha = (p.normalizedAge / 0.2f) * 0.35f;
        else if (p.normalizedAge > 0.7f)
            alpha = ((1.f - p.normalizedAge) / 0.3f) * 0.35f;
        else
            alpha = 0.35f;

        // Pre-multiply
        const glm::vec3 baseRgb = glm::vec3(p.color) / std::max(p.color.a, 0.001f);
        p.color = glm::vec4(baseRgb * alpha, alpha);
        return true;
    });

    // Sort back-to-front for correct premul alpha layering
    sorted_.resize(pool_.liveCount());
    std::copy(pool_.rawData(), pool_.rawData() + pool_.liveCount(), sorted_.begin());
    std::sort(sorted_.begin(), sorted_.end(), [&](const SmokeParticle& a, const SmokeParticle& b) {
        return glm::dot(a.pos - camPos, camForward) > glm::dot(b.pos - camPos, camForward);
    });
}
