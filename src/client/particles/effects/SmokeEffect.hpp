#pragma once

#include "ecs/registry/Registry.hpp"
#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <vector>

/// @brief Manages volumetric smoke billboard particles.
///
/// Provides spawn() for one-shot smoke puffs and also drives
/// ParticleEmitterTag entities continuously in update().
///
/// Particles are sorted back-to-front before upload for correct alpha blending.
class SmokeEffect
{
public:
    void update(float dt, Registry& registry, glm::vec3 camPos, glm::vec3 camForward);

    /// @brief Spawn a cluster of smoke puffs at pos with the given radius.
    void spawn(glm::vec3 pos, float radius, bool isFire = false);

    [[nodiscard]] const SmokeParticle* data() const { return sorted_.data(); }
    [[nodiscard]] uint32_t count() const { return static_cast<uint32_t>(sorted_.size()); }

private:
    ParticlePool<SmokeParticle, 1024> pool_;
    std::vector<SmokeParticle> sorted_;      // back-to-front sorted copy for upload

    static constexpr float k_upDrift = 18.f; ///< Upward drift speed (world units/s).
};
