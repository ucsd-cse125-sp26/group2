#pragma once

#include "ecs/components/Projectile.hpp"
#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"

#include <glm/glm.hpp>

/// @brief Spawns spark bursts + impact flash quads on hit events.
class ImpactEffect
{
public:
    void update(float dt);

    /// @brief Spawn 8–16 sparks + 1 flash at the impact point.
    void spawn(glm::vec3 pos, glm::vec3 normal, SurfaceType surface, float frameDt);

    [[nodiscard]] const BillboardParticle* data() const { return pool_.rawData(); }
    [[nodiscard]] uint32_t count() const { return pool_.liveCount(); }

private:
    ParticlePool<BillboardParticle, 4096> pool_;
};
