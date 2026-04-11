#pragma once

#include "ecs/components/Projectile.hpp"
#include "particles/ParticleTypes.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

/// @brief Ring-buffer pool of 512 world-space decal quads (bullet holes, scorch marks).
///
/// Oldest decal is overwritten when the pool is full.  All 512 slots are uploaded
/// to the GPU every frame (24 KB — cheap) so there is no kill/compact logic.
class BulletHoleDecal
{
public:
    /// @brief Place a new decal at pos oriented by normal.
    void spawn(glm::vec3 pos, glm::vec3 normal, WeaponType wt);

    /// @brief Fade all active decals.
    void update(float dt);

    [[nodiscard]] const DecalInstance* data() const { return slots_.data(); }
    [[nodiscard]] uint32_t count() const { return std::min(head_, k_max); }

private:
    static constexpr uint32_t k_max = 512;
    std::array<DecalInstance, k_max> slots_{};
    uint32_t head_ = 0; ///< Ring-buffer insertion pointer.
};
