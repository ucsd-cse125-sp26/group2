/// @file ExplosionEffect.cpp
/// @brief Implementation of shockwave ring and fireball explosion effect.

#include "ExplosionEffect.hpp"

#include <algorithm>

void ExplosionEffect::spawn(glm::vec3 pos, float blastRadius, SmokeEffect& smoke)
{
    // 1. Shockwave ring (single billboard; fragment shader draws ring shape)
    if (auto* ring = ringPool_.spawn()) {
        ring->pos = pos;
        ring->size = 0.f;                                    // starts at 0, expands in update
        ring->color = glm::vec4{1.f, 0.6f, 0.2f, 0.9f};
        ring->vel = glm::vec3{blastRadius * 1.5f, 0.f, 0.f}; // .x = target size
        ring->lifetime = 0.3f;
    }

    // 2. Immediate fireballs (3-5 fire smoke particles)
    smoke.spawn(pos, blastRadius * 0.4f, /*isFire=*/true);
    smoke.spawn(pos, blastRadius * 0.3f, /*isFire=*/true);

    // 3. Deferred smoke cloud (spawns 0.1 s later)
    if (pendingCount_ < k_maxPending) {
        pending_[pendingCount_++] = {pos, blastRadius * 0.8f, 0.1f, false};
    }
}

void ExplosionEffect::update(float dt)
{
    // Animate rings: size ramps toward vel.x (target size) over lifetime
    ringPool_.update([&](BillboardParticle& r) -> bool {
        r.lifetime -= dt;
        if (r.lifetime <= 0.f)
            return false;

        const float progress = 1.f - (r.lifetime / 0.3f);
        r.size = r.vel.x * progress;           // expand toward target size
        r.color.a = 1.f - progress * progress; // fade out quadratically
        return true;
    });

    // Tick deferred smoke spawns
    // (We can't call smoke.spawn here without a SmokeEffect ref -- caller must handle)
    for (int i = 0; i < pendingCount_; ++i) {
        pending_[i].delay -= dt;
    }
}
