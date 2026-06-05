/// @file DeathDissolveEffect.hpp
/// @brief "Thanos snap" death dissolve — a character's last pose crumbles into
///        wind-swept particles that drift away and fade.

#pragma once

#include "particles/ParticleTypes.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

/// @brief One-shot disintegration effect spawned from a character's skinned
/// mesh at the moment of death.
///
/// The caller samples the dead character's posed mesh into world-space points
/// (see `Game`'s death-capture pass) and hands them to `spawn`. Each point
/// becomes a particle that holds at its mesh position for a short, position-
/// dependent delay (producing a sweeping dissolve wave), then peels off along a
/// wind direction, drifts, and fades. Particles are alpha-blended camera-facing
/// billboards rendered through the shared particle-billboard shaders.
class DeathDissolveEffect
{
public:
    /// @brief Spawn a dissolve from posed mesh points.
    /// @param worldPoints  World-space positions sampled from the death pose.
    /// @param center       Body center (world) — drives the radial/sweep basis.
    /// @param baseColor    Particle tint (mesh has no texture; ~light grey).
    void spawn(const std::vector<glm::vec3>& worldPoints, glm::vec3 center, glm::vec4 baseColor);

    /// @brief Advance the simulation and rebuild the GPU billboard array.
    void update(float dt);

    /// @brief Live particle data for upload (rebuilt each `update`).
    [[nodiscard]] const BillboardParticle* data() const { return gpu_.data(); }
    [[nodiscard]] uint32_t count() const { return static_cast<uint32_t>(gpu_.size()); }

private:
    /// @brief Full CPU simulation state for one dissolve particle.
    struct Particle
    {
        glm::vec3 basePos;  ///< Resting (pre-dissolve) world position.
        glm::vec3 vel;      ///< Drift velocity once active.
        glm::vec4 color;    ///< Base tint (alpha is faded per frame).
        float size = 1.5f;  ///< Billboard half-extent (world units).
        float delay = 0.0f; ///< Seconds to hold at basePos before drifting.
        float age = 0.0f;   ///< Seconds since spawn.
        float fly = 1.5f;   ///< Drift+fade duration after the delay.
    };

    static constexpr uint32_t k_maxParticles = 30000;

    std::vector<Particle> particles_;
    std::vector<BillboardParticle> gpu_; ///< Rebuilt every frame for upload.
};
