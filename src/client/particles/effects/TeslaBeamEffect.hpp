/// @file TeslaBeamEffect.hpp
/// @brief Continuous, curved energy-arc beam for the auto-lock Tesla Cannon.
///
/// Adapted from HitscanEffect (the instant fBm lightning bolt) but reworked for
/// a *sustained* beam that visibly **bows toward its locked target** instead of
/// snapping in a straight line from barrel to hit point. Each active beam is
/// driven every frame by the owning entity's BeamState (origin + locked-target
/// point); when the owner stops firing the beam is no longer driven and fades.
///
/// Signal chain per frame (per beam):
///   t [0,1] along a deeply-bowed cubic Bezier spine (sag toward target)
///     -> domain-warped 4-octave fBm crackle, perpendicular to the spine
///     -> sin(t*pi) envelope (pinned at both endpoints)
///   plus a few forked branches for the Winston "tesla" look.
///
/// Rendering: three additive triangle-strip layers (bloom / channel / white-hot
/// core) in the energy-green palette, matching the beam point-light colour.

#pragma once

#include "particles/ParticleTypes.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

/// @brief Sustained curved energy-arc beams, keyed by owner entity id.
class TeslaBeamEffect
{
public:
    /// @brief Refresh (or start) the beam owned by `key` for this frame.
    ///
    /// Call once per frame for every entity whose Tesla beam is firing. The beam
    /// stays alive as long as it keeps being driven; stop calling drive() and it
    /// fades out over `k_fadeTime`.
    /// @param key    Stable owner id (entity handle as integer).
    /// @param origin World-space beam start (muzzle / eye).
    /// @param target World-space beam end (the locked target, or a forward point).
    void drive(uint32_t key, glm::vec3 origin, glm::vec3 target);

    /// @brief Advance animation, age out undriven beams, and rebuild arc verts.
    /// @param dt         Frame delta time in seconds.
    /// @param camForward Camera forward vector for billboard orientation.
    void update(float dt, glm::vec3 camForward);

    /// @brief Arc vertex array for GPU upload (nullptr if empty).
    [[nodiscard]] const ArcVertex* arcData() const { return arcVerts_.data(); }

    /// @brief Number of arc vertices generated this frame.
    [[nodiscard]] uint32_t arcCount() const { return static_cast<uint32_t>(arcVerts_.size()); }

    /// @brief Number of beams currently alive.
    [[nodiscard]] uint32_t activeBeamCount() const;

private:
    static constexpr int k_maxBeams = 4;
    static constexpr int k_bezierSegs = 28; ///< Samples along the main arc.
    static constexpr int k_branchSegs = 6;  ///< Samples along each branch.
    static constexpr int k_maxBranches = 3;
    static constexpr float k_fadeTime = 0.08f;  ///< Fade in/out window (s).
    static constexpr float k_keepAlive = 0.06f; ///< Grace before an undriven beam starts fading.
    static constexpr float k_bowFrac = 0.16f;   ///< Spine sag as a fraction of beam length.
    static constexpr float k_branchRetime = 0.05f; ///< Branch re-randomise interval (s).

    /// @brief One forked branch diverging from the main arc.
    struct Branch
    {
        float tStart; ///< Position along main arc [0,1].
        float length; ///< Length as fraction of main bolt length.
        float angle;  ///< Divergence from tangent (radians).
        float seed;   ///< Independent fBm seed.
    };

    /// @brief State for a single sustained, curved beam.
    struct Beam
    {
        uint32_t key = 0;
        bool active = false;
        glm::vec3 origin{0.0f};
        glm::vec3 target{0.0f};

        float age = 0.f;       ///< Time since creation (drives fade-in).
        float keepAlive = 0.f; ///< Counts down while undriven (drives fade-out).
        float time = 0.f;      ///< fBm animation clock.
        float seed = 0.f;
        float warpSeed = 0.f;

        Branch branches[k_maxBranches]{};
        int branchCount = 0;
        float branchTimer = 0.f;
    };

    Beam beams_[k_maxBeams]{};
    std::vector<ArcVertex> arcVerts_;

    static glm::vec3 evalBezier(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t);
    static glm::vec3 evalBezierTangent(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t);
    static void rerandomizeBranches(Beam& beam);

    /// @brief Append a camera-facing triangle-strip ribbon to arcVerts_.
    void appendArcStrip(const std::vector<glm::vec3>& pts, float radius, glm::vec4 color, glm::vec3 camForward);

    Beam* findOrAllocBeam(uint32_t key);
};
