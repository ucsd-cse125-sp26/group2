#pragma once

#include "ecs/components/Projectile.hpp"
#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <vector>

/// @brief Hitscan energy beam (main glow quad + CPU-generated lightning arcs).
class HitscanEffect
{
public:
    void update(float dt);

    /// @brief Spawn a beam from origin to hitPos with arc fringe effects.
    void spawn(glm::vec3 origin, glm::vec3 hitPos, WeaponType wt, glm::vec3 camForward);

    // Main beam instances
    [[nodiscard]] const HitscanBeam* beamData() const { return beamPool_.rawData(); }
    [[nodiscard]] uint32_t beamCount() const { return beamPool_.liveCount(); }

    // Lightning arc vertex stream (rebuilt each frame during update)
    [[nodiscard]] const ArcVertex* arcData() const { return arcVerts_.data(); }
    [[nodiscard]] uint32_t arcCount() const { return static_cast<uint32_t>(arcVerts_.size()); }

private:
    ParticlePool<HitscanBeam, 64> beamPool_;

    struct LiveArc
    {
        std::vector<ArcVertex> verts;
        float lifetime;
        float maxLifetime;
    };
    std::vector<LiveArc> arcs_;
    std::vector<ArcVertex> arcVerts_; // flat merged, rebuilt each frame

    void generateArcs(glm::vec3 origin, glm::vec3 hitPos, glm::vec3 camForward, glm::vec4 color, float maxLifetime);

    static void displaceSegment(std::vector<glm::vec3>& pts, int depth, float maxOffset, glm::vec3 camForward);
    static void
    expandArcToVerts(const std::vector<glm::vec3>& pts, float arcRadius, glm::vec4 color, std::vector<ArcVertex>& out);

    static constexpr float k_beamLifetime = 0.12f;
    static constexpr float k_arcRadius = 0.35f; // narrow — subtle electrical discharge
};
