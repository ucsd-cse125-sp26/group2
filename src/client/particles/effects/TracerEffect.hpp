#pragma once

#include "ecs/registry/Registry.hpp"
#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <unordered_map>

/// @brief Manages oriented-capsule tracers for fast-bullet projectile entities.
///
/// Each frame, update() polls registry entities with TracerEmitter components
/// and updates the tip/tail of their associated TracerParticle.
class TracerEffect
{
public:
    /// @brief Update all live tracers. registry is used to read projectile positions.
    void update(float dt, Registry& registry);

    /// @brief Attach a new tracer to a projectile entity.
    void attach(entt::entity e, Registry& registry);

    /// @brief Detach the tracer from an entity (entity dying); tracer fades out.
    void detach(entt::entity e);

    [[nodiscard]] const TracerParticle* data() const { return pool_.rawData(); }
    [[nodiscard]] uint32_t count() const { return pool_.liveCount(); }

private:
    ParticlePool<TracerParticle, 512> pool_;

    // Maps entity → index into pool (for fast detach / per-entity update)
    std::unordered_map<uint32_t, uint32_t> entityToIdx_;

    static constexpr float k_streakLength = 180.f; ///< Visual streak length in world units.
    static constexpr float k_fadeTime = 0.1f;      ///< Seconds to fade after entity death.
};
