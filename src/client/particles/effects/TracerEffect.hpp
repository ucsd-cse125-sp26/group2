/// @file TracerEffect.hpp
/// @brief Oriented-capsule tracer management for fast-bullet projectile entities.

#pragma once

#include "ecs/registry/Registry.hpp"
#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>

/// @brief Manages oriented-capsule tracers for fast-bullet projectile entities.
///
/// Each frame, update() polls registry entities with TracerEmitter components
/// and updates the tip/tail of their associated TracerParticle.
class TracerEffect
{
public:
    /// @brief Update all live tracers. Registry is used to read projectile positions.
    /// @param dt       Frame delta time in seconds.
    /// @param registry ECS registry containing TracerEmitter entities.
    void update(float dt, Registry& registry);

    /// @brief Attach a new tracer to a projectile entity.
    /// @param e        Entity handle of the projectile.
    /// @param registry ECS registry to read Position and TracerEmitter from.
    void attach(entt::entity e, Registry& registry);

    /// @brief Detach the tracer from an entity (entity dying); tracer fades out.
    /// @param e Entity handle being destroyed.
    void detach(entt::entity e);

    /// @brief Spawn a visual-only rifle bullet that travels along a hitscan path.
    /// @param origin Muzzle world position.
    /// @param dir    Normalized or normalizable fire direction.
    /// @param range  Visual travel distance in world units.
    void spawnRifleTracer(glm::vec3 origin, glm::vec3 dir, float range);

    [[nodiscard]] const TracerParticle* data() const { return pool_.rawData(); }
    [[nodiscard]] uint32_t count() const { return pool_.liveCount(); }

private:
    static constexpr uint32_t k_maxTracers = 512;

    enum class RuntimeKind : uint8_t
    {
        None,
        Entity,
        RifleProjectile,
    };

    struct RuntimeState
    {
        RuntimeKind kind = RuntimeKind::None;
        entt::entity entity = entt::null;
        glm::vec3 origin{};
        glm::vec3 dir{0.0f, 0.0f, 1.0f};
        float distance = 0.0f;
        float speed = 0.0f;
        float trailLength = 0.0f;
        float age = 0.0f;
    };

    ParticlePool<TracerParticle, k_maxTracers> pool_;
    std::array<RuntimeState, k_maxTracers> runtime_{};

    // Maps entity -> index into pool (for fast detach / per-entity update)
    std::unordered_map<uint32_t, uint32_t> entityToIdx_;

    void killTracer(uint32_t idx);
    void updateRifleTracer(uint32_t idx, float dt);

    static constexpr float k_streakLength = 200.f; ///< Visual streak length in world units.
    static constexpr float k_fadeTime = 0.15f;     ///< Seconds to fade after entity death.
    static constexpr float k_rifleVisualSpeed = 8000.f;
    static constexpr float k_rifleTrailLength = 140.f;
    static constexpr float k_rifleSpawnLead = 75.f;
    static constexpr float k_rifleRadius = 2.35f;
};
