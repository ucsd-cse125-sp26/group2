/// @file SmokeEffect.hpp
/// @brief Volumetric smoke billboard particle manager with back-to-front sorting.

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
    /// @brief Simulate particles, drive continuous emitters, and sort for rendering.
    /// @param dt         Frame delta time in seconds.
    /// @param registry   ECS registry containing ParticleEmitterTag entities.
    /// @param camPos     World-space camera position for depth sorting.
    /// @param camForward Camera forward vector for depth sorting.
    void update(float dt, Registry& registry, glm::vec3 camPos, glm::vec3 camForward);

    /// @brief Spawn a cluster of smoke puffs at pos with the given radius.
    /// @param pos    World-space center of the smoke cluster.
    /// @param radius Spread radius for particle placement.
    /// @param isFire If true, spawn fire-colored particles instead of grey smoke.
    void spawn(glm::vec3 pos, float radius, bool isFire = false);

    [[nodiscard]] const SmokeParticle* data() const { return sorted_.data(); }
    [[nodiscard]] uint32_t count() const { return static_cast<uint32_t>(sorted_.size()); }

private:
    ParticlePool<SmokeParticle, 1024> pool_;
    std::vector<SmokeParticle> sorted_;      ///< Back-to-front sorted copy for upload.

    static constexpr float k_upDrift = 18.f; ///< Upward drift speed (world units/s).
};
