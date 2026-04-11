#pragma once

#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"
#include "particles/effects/SmokeEffect.hpp"

#include <glm/glm.hpp>

/// @brief Spawns shockwave ring + fireball + smoke cloud for rocket explosions.
///
/// Reuses BillboardParticle for the shockwave ring and SmokeEffect for fire/smoke.
class ExplosionEffect
{
public:
    void update(float dt);

    /// @brief Spawn a full explosion at pos with given blast radius.
    void spawn(glm::vec3 pos, float blastRadius, SmokeEffect& smoke);

    // Shockwave ring particles
    [[nodiscard]] const BillboardParticle* ringData() const { return ringPool_.rawData(); }
    [[nodiscard]] uint32_t ringCount() const { return ringPool_.liveCount(); }

private:
    // We need a separate pool for shockwave rings — different render behaviour from sparks
    // but same BillboardParticle struct (size drives ring scale, color encodes ring vs disc).
    ParticlePool<BillboardParticle, 64> ringPool_;

    struct PendingSmoke
    {
        glm::vec3 pos;
        float radius;
        float delay; // seconds until spawn
        bool isFire;
    };
    // Holds deferred smoke/fire spawns
    static constexpr int k_maxPending = 32;
    PendingSmoke pending_[k_maxPending]{};
    int pendingCount_ = 0;
};
