/// @file DeathDissolveEffect.cpp
/// @brief Implementation of the Thanos-snap death dissolve.

#include "DeathDissolveEffect.hpp"

#include <algorithm>
#include <cmath>

namespace
{
// Deterministic per-particle pseudo-random in [0,1) from an integer key.
// Avoids rand()/Math.random so the effect is thread-safe and reproducible.
float hash01(uint32_t x)
{
    x = (x ^ 61u) ^ (x >> 16);
    x *= 9u;
    x ^= x >> 4;
    x *= 0x27d4eb2du;
    x ^= x >> 15;
    return static_cast<float>(x & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

// Wind the ashes drift along (up and slightly to the side), Thanos-style.
const glm::vec3 k_windDir = glm::normalize(glm::vec3(0.45f, 0.78f, 0.30f));
constexpr float k_sweepSeconds = 0.55f; ///< How long the dissolve wave takes to cross the body.
constexpr float k_bodyHalf = 36.0f;     ///< Approx body half-height (world units) for sweep normalisation.
} // namespace

void DeathDissolveEffect::spawn(const std::vector<glm::vec3>& worldPoints, glm::vec3 center, glm::vec4 baseColor)
{
    if (worldPoints.empty())
        return;

    // Cap total live particles: drop the oldest if a new burst would overflow.
    if (particles_.size() + worldPoints.size() > k_maxParticles) {
        const size_t overflow = particles_.size() + worldPoints.size() - k_maxParticles;
        const size_t drop = std::min(overflow, particles_.size());
        particles_.erase(particles_.begin(), particles_.begin() + static_cast<std::ptrdiff_t>(drop));
    }

    particles_.reserve(particles_.size() + worldPoints.size());
    for (size_t i = 0; i < worldPoints.size(); ++i) {
        const glm::vec3 p = worldPoints[i];
        const auto key = static_cast<uint32_t>(i * 2654435761u);
        const float r0 = hash01(key);
        const float r1 = hash01(key ^ 0x9e3779b9u);
        const float r2 = hash01(key ^ 0x85ebca6bu);
        const float r3 = hash01(key ^ 0xc2b2ae35u);

        // Horizontal radial direction (so the body also puffs outward, not just
        // drifts as a slab). Falls back to a hashed direction at the center.
        glm::vec3 radial = p - center;
        radial.y = 0.0f;
        const float radLen = glm::length(radial);
        const glm::vec3 radialDir =
            (radLen > 1e-3f) ? (radial / radLen) : glm::normalize(glm::vec3(r0 - 0.5f, 0.0f, r1 - 0.5f) + 1e-3f);

        const glm::vec3 jitter{(r0 - 0.5f) * 16.0f, (r1 - 0.5f) * 16.0f, (r2 - 0.5f) * 16.0f};

        Particle pt;
        pt.basePos = p;
        pt.vel = k_windDir * (28.0f + 34.0f * r3) // primary up-and-away drift
                 + radialDir * (6.0f + 12.0f * r1) // gentle outward puff
                 + jitter;
        pt.color = baseColor;
        pt.size = 1.1f + 1.1f * r2;
        // Sweep delay: particles further "downwind" along the body dissolve
        // later, producing the signature crumbling wave. Plus a little jitter.
        const float along = glm::dot(p - center, k_windDir);            // ~[-bodyHalf, +bodyHalf]
        const float t01 = std::clamp((k_bodyHalf - along) / (2.0f * k_bodyHalf), 0.0f, 1.0f);
        pt.delay = t01 * k_sweepSeconds + r0 * 0.12f;
        pt.age = 0.0f;
        pt.fly = 1.35f + 0.5f * r1;
        particles_.push_back(pt);
    }
}

void DeathDissolveEffect::update(float dt)
{
    constexpr glm::vec3 k_accel{0.0f, -6.0f, 0.0f}; // slight settle so ashes don't rise forever

    gpu_.clear();
    if (particles_.empty())
        return;
    gpu_.reserve(particles_.size());

    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.age += dt;

        const float total = p.delay + p.fly;
        if (p.age >= total) {
            // Dead — O(1) swap-remove.
            p = particles_.back();
            particles_.pop_back();
            continue;
        }

        BillboardParticle b;
        b.size = p.size;
        b.vel = glm::vec3(0.0f); // shader ignores; sim is CPU-side
        if (p.age <= p.delay) {
            // Holding in the last pose — full opacity, no motion yet.
            b.pos = p.basePos;
            b.color = p.color;
        } else {
            const float ft = p.age - p.delay;          // time spent drifting
            const float u = std::clamp(ft / p.fly, 0.0f, 1.0f);
            b.pos = p.basePos + p.vel * ft + 0.5f * k_accel * (ft * ft);
            b.color = p.color;
            b.color.a = p.color.a * (1.0f - u * u); // fade out, accelerating
            b.size = p.size * (1.0f - 0.5f * u);    // shrink slightly as it fades
        }
        gpu_.push_back(b);
        ++i;
    }
}
