/// @file ImpactEffect.cpp
/// @brief Implementation of spark burst and impact flash effect.

#include "ImpactEffect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
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

// Per-surface visual parameters
/// @brief Visual tuning parameters for a single surface material type.
struct SurfaceParams
{
    glm::vec4 color;
    float speedMin, speedMax;
    float sizeMin, sizeMax;
    int count;
};

static constexpr SurfaceParams k_surfaces[] = {
    // Metal   -- bright white-yellow sparks, high velocity
    {{1.0f, 0.90f, 0.60f, 1.0f}, 300.f, 600.f, 0.8f, 1.5f, 14},
    // Concrete -- grey dust, medium velocity
    {{0.7f, 0.65f, 0.60f, 1.0f}, 150.f, 350.f, 1.2f, 2.5f, 10},
    // Flesh    -- dark red, lower velocity, larger puffs
    {{0.8f, 0.10f, 0.10f, 1.0f}, 100.f, 250.f, 1.5f, 3.0f, 8},
    // Wood     -- brown splinters
    {{0.6f, 0.40f, 0.20f, 1.0f}, 120.f, 300.f, 1.0f, 2.0f, 10},
    // Energy   -- cyan sparks, very fast
    {{0.2f, 0.80f, 1.00f, 1.0f}, 400.f, 700.f, 0.5f, 1.2f, 16},
};

void ImpactEffect::spawn(glm::vec3 pos, glm::vec3 normal, SurfaceType surface, float frameDt)
{
    const auto& p = k_surfaces[static_cast<int>(surface)];

    // Compute tangent basis for cone spread
    const glm::vec3 up = (std::abs(normal.y) > 0.9f) ? glm::vec3{1.f, 0.f, 0.f} : glm::vec3{0.f, 1.f, 0.f};
    const glm::vec3 right = glm::normalize(glm::cross(normal, up));
    const glm::vec3 fwd = glm::cross(right, normal);

    constexpr float k_coneAngle = glm::pi<float>() * 55.f / 180.f; // 55 deg half-angle

    for (int i = 0; i < p.count; ++i) {
        auto* sp = pool_.spawn();
        if (!sp)
            break;

        // Random direction within cone around surface normal
        const float theta = randf() * 2.f * glm::pi<float>();
        const float phi = randf() * k_coneAngle;
        const glm::vec3 dir =
            glm::normalize(normal * std::cos(phi) + (right * std::cos(theta) + fwd * std::sin(theta)) * std::sin(phi));

        sp->pos = pos;
        sp->size = randRange(p.sizeMin, p.sizeMax);
        sp->color = p.color;
        sp->vel = dir * randRange(p.speedMin, p.speedMax);
        sp->lifetime = randRange(0.25f, 0.45f);
    }

    // Impact flash: single oversized billboard that lives exactly one frame
    if (auto* flash = pool_.spawn()) {
        flash->pos = pos;
        flash->size = 20.f;
        flash->color = glm::vec4{1.f, 0.95f, 0.8f, 1.f};
        flash->vel = glm::vec3{0.f};
        flash->lifetime = frameDt + 0.001f;
    }
}

void ImpactEffect::update(float dt)
{
    constexpr float k_gravity = 1000.f; // Quake units/s squared

    pool_.update([&](BillboardParticle& p) -> bool {
        p.lifetime -= dt;
        if (p.lifetime <= 0.f)
            return false;
        p.vel.y -= k_gravity * dt;
        p.pos += p.vel * dt;
        // Fade alpha based on remaining lifetime
        const float normAge = 1.f - (p.lifetime / 0.45f);
        p.color.a = 1.f - normAge * normAge;
        return true;
    });
}
