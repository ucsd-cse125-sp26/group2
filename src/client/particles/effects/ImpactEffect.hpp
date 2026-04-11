/// @file ImpactEffect.hpp
/// @brief Spark burst and impact flash effect for projectile hits.

#pragma once

#include "ecs/components/Projectile.hpp"
#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"

#include <glm/glm.hpp>

/// @brief Spawns spark bursts + impact flash quads on hit events.
class ImpactEffect
{
public:
    /// @brief Advance spark physics and fade alpha over time.
    void update(float dt);

    /// @brief Spawn 8-16 sparks + 1 flash at the impact point.
    /// @param pos     World-space impact position.
    /// @param normal  Surface normal at the impact point.
    /// @param surface Surface material type controlling spark appearance.
    /// @param frameDt Current frame delta time, used for single-frame flash lifetime.
    void spawn(glm::vec3 pos, glm::vec3 normal, SurfaceType surface, float frameDt);

    [[nodiscard]] const BillboardParticle* data() const { return pool_.rawData(); }
    [[nodiscard]] uint32_t count() const { return pool_.liveCount(); }

private:
    ParticlePool<BillboardParticle, 4096> pool_;
};
