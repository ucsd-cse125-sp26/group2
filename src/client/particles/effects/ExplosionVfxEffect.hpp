/// @file ExplosionVfxEffect.hpp
/// @brief Fresh layered explosion, fire, smoke, spark, and scorch VFX.

#pragma once

#include "ecs/components/FireField.hpp"
#include "ecs/components/Projectile.hpp"
#include "ecs/registry/Registry.hpp"
#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

/// @brief Owns the new explosion VFX path for rockets, frags, stickies, and molotovs.
///
/// This system intentionally does not call ExplosionEffect or SmokeEffect. Fire,
/// smoke, dust, shock rings, embers, debris, scorch decals, and molotov ground
/// flames are authored here as typed layers.
class ExplosionVfxEffect
{
public:
    /// @brief Advance all active explosion VFX and persistent ground fire anchors.
    void update(float dt, Registry& registry, glm::vec3 camPos, glm::vec3 camForward);

    /// @brief Spawn a one-shot explosion burst.
    void spawn(glm::vec3 pos, glm::vec3 normal, float radius, ExplosionVfxKind kind);

    /// @brief Drive persistent molotov ground-fire visuals from a replicated FireField.
    void driveGroundFire(entt::entity fieldEntity, glm::vec3 pos, float radius, float remaining, float duration);

    [[nodiscard]] const VfxSpriteParticle* spriteData() const { return sortedSprites_.data(); }
    [[nodiscard]] uint32_t spriteCount() const { return static_cast<uint32_t>(sortedSprites_.size()); }

    [[nodiscard]] const VfxDebrisParticle* debrisData() const { return debrisPool_.rawData(); }
    [[nodiscard]] uint32_t debrisCount() const { return debrisPool_.liveCount(); }

    [[nodiscard]] const DecalInstance* decalData() const { return decals_.data(); }
    [[nodiscard]] uint32_t decalCount() const { return std::min(decalHead_, k_maxDecals); }

private:
    struct GroundFireAnchor
    {
        entt::entity entity = entt::null;
        glm::vec3 pos{0.0f};
        float radius = 0.0f;
        float remaining = 0.0f;
        float duration = 0.0f;
        float flameAccumulator = 0.0f;
        float smokeAccumulator = 0.0f;
        float emberAccumulator = 0.0f;
        bool spawnedBurst = false;
        uint32_t seed = 0;
    };

    ParticlePool<VfxSpriteParticle, 2048> spritePool_;
    ParticlePool<VfxDebrisParticle, 1024> debrisPool_;
    std::vector<VfxSpriteParticle> sortedSprites_;

    static constexpr uint32_t k_maxDecals = 256;
    std::array<DecalInstance, k_maxDecals> decals_{};
    uint32_t decalHead_ = 0;

    std::vector<GroundFireAnchor> groundFire_;
    uint32_t sequence_ = 1;

    void spawnSprite(glm::vec3 pos,
                     glm::vec3 vel,
                     float size,
                     glm::vec4 color,
                     float lifetime,
                     float growthRate,
                     float spinRate,
                     float rotation,
                     int frameStart,
                     int frameCount,
                     float fps,
                     int material,
                     float stretch,
                     float softness,
                     float priority);
    void spawnDebris(glm::vec3 pos,
                     glm::vec3 vel,
                     float size,
                     glm::vec4 color,
                     float lifetime,
                     float gravity,
                     float drag,
                     float stretch);
    void spawnScorch(glm::vec3 pos, glm::vec3 normal, float size, float opacity, ExplosionVfxKind kind);

    void spawnRocket(glm::vec3 pos, glm::vec3 normal, float radius);
    void spawnFrag(glm::vec3 pos, glm::vec3 normal, float radius);
    void spawnSticky(glm::vec3 pos, glm::vec3 normal, float radius);
    void spawnMolotovBurst(glm::vec3 pos, glm::vec3 normal, float radius);
    void tickGroundFire(GroundFireAnchor& anchor, float dt);

    [[nodiscard]] uint32_t nextSeed() { return sequence_++ * 747796405u + 2891336453u; }
};

[[nodiscard]] ExplosionVfxKind explosionVfxKindForWeapon(WeaponType type);
