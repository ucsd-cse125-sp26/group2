/// @file HitscanEffect.hpp
/// @brief Hitscan energy beam effect using fBm path deviation and Bezier spines.

#pragma once

#include "ecs/components/Projectile.hpp"
#include "particles/ParticleTypes.hpp"

#include <glm/glm.hpp>
#include <vector>

/// @brief Hitscan energy beam using fBm (fractional Brownian motion) path deviation.
///
/// Signal chain per frame:
///   t [0,1] along Bezier spine
///     -> optional domain warp
///     -> 4-octave fBm (lacunarity~3, persistence~0.4)
///     -> multiply by sin(t*pi) envelope  (pinned at both endpoints)
///     -> scale by baseAmplitude (16 % of beam length)
///     -> apply perpendicular to Bezier tangent
///
/// Rendering: three concentric triangle-strip layers per arc (outer bloom halo,
/// inner glow channel, white-hot core) rendered with additive blending, giving
/// an HDR-like look without a post-process pass.
///
/// Temporal behaviour:
///   - Smooth Bezier morph when a new shot is fired (80 ms interpolation).
///   - Continuous fBm animation (each octave runs at 2x the speed of the one below).
///   - 2-3 return strokes scheduled at 60/120/180 ms after the initial shot;
///     each re-randomises the arc path and branches, simulating multi-strike behaviour.
///   - Branches re-randomise every 45 ms for rapid flicker.
class HitscanEffect
{
public:
    /// @brief Update all active beams, rebuilding arc vertex data each frame.
    /// @param dt         Frame delta time in seconds.
    /// @param camForward Camera forward vector for billboard orientation.
    void update(float dt, glm::vec3 camForward);

    /// @brief Fire a beam.  Consecutive shots on the same slot produce a smooth morph.
    /// @param origin     World-space beam start position.
    /// @param hitPos     World-space beam end position.
    /// @param wt         Weapon type (reserved for future per-weapon tuning).
    /// @param camForward Camera forward vector for control-point placement.
    void spawn(glm::vec3 origin, glm::vec3 hitPos, WeaponType wt, glm::vec3 camForward);

    /// @brief Get pointer to the arc vertex array for GPU upload.
    /// @return Pointer to the first ArcVertex, or nullptr if empty.
    [[nodiscard]] const ArcVertex* arcData() const { return arcVerts_.data(); }

    /// @brief Get the number of arc vertices currently generated.
    /// @return Vertex count.
    [[nodiscard]] uint32_t arcCount() const { return static_cast<uint32_t>(arcVerts_.size()); }

    /// @brief Get the number of active beams.
    /// @return Count of beams with active == true.
    [[nodiscard]] uint32_t activeBeamCount() const;

    // Legacy API compatibility -- beam quad pool not used; everything is arc verts.
    [[nodiscard]] const HitscanBeam* beamData() const { return nullptr; }
    [[nodiscard]] uint32_t beamCount() const { return 0; }

private:
    // Tuning constants
    static constexpr int k_maxBeams = 4;
    static constexpr int k_bezierSegs = 32; ///< Samples along main arc.
    static constexpr int k_branchSegs = 7;  ///< Samples along each branch.
    static constexpr int k_maxBranches = 5;
    static constexpr float k_beamLifetime = 0.22f;
    static constexpr float k_interpDuration = 0.08f; ///< Bezier morph time (s).

    // Internal data structures

    /// @brief A single forked branch diverging from the main arc.
    struct Branch
    {
        float tStart; ///< Position along main arc [0,1].
        float length; ///< World length as fraction of main bolt length.
        float angle;  ///< Divergence from tangent (radians).
        float seed;   ///< Independent fBm seed.
    };

    /// @brief A scheduled return stroke that re-randomises the arc path.
    struct ReturnStroke
    {
        float fireAt = 0.f; ///< Elapsed seconds after initial spawn.
        float seed = 0.f;
        bool fired = false;
    };

    /// @brief State for a single active Bezier-spine beam.
    struct BezierBeam
    {
        glm::vec3 origin, hitPos;

        // Cubic Bezier CPs -- interpolated from prev -> curr over k_interpDuration.
        glm::vec3 cp1Curr, cp2Curr;
        glm::vec3 cp1Prev, cp2Prev;
        float interpT = 1.f;

        // fBm animation time (accumulates each frame).
        float time = 0.f;
        float noiseSeed = 0.f; ///< Base fBm seed (re-rolled each return stroke).
        float warpSeed = 0.f;  ///< Domain warp seed.

        Branch branches[k_maxBranches]{};
        int branchCount = 0;
        float branchTimer = 0.f;
        static constexpr float k_branchRetime = 0.045f; ///< Branch re-randomize interval.

        ReturnStroke returnStrokes[3]{};

        float lifetime = 0.f;
        bool active = false;
    };

    BezierBeam beams_[k_maxBeams]{};
    std::vector<ArcVertex> arcVerts_;

    // Helpers

    /// @brief Evaluate a cubic Bezier curve at parameter t.
    static glm::vec3 evalBezier(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t);

    /// @brief Evaluate the tangent of a cubic Bezier curve at parameter t.
    static glm::vec3 evalBezierTangent(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t);

    /// @brief Randomize Bezier control points for a new arc shape.
    static void randomizeCP(glm::vec3 origin, glm::vec3 hitPos, glm::vec3 camForward, glm::vec3& cp1, glm::vec3& cp2);

    /// @brief Re-randomize all branch parameters on a beam for flicker.
    static void rerandomizeBranches(BezierBeam& beam, glm::vec3 camForward);

    /// @brief Append a camera-facing triangle-strip ribbon to arcVerts_.
    /// @param pts       Sampled points along the arc segment.
    /// @param radius    Half-width of the ribbon strip.
    /// @param color     RGBA color for all vertices in this strip.
    /// @param camForward Camera forward vector for billboard orientation.
    void appendArcStrip(const std::vector<glm::vec3>& pts, float radius, glm::vec4 color, glm::vec3 camForward);
};
