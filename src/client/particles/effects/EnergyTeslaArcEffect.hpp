/// @file EnergyTeslaArcEffect.hpp
/// @brief Sustained Railgun-style dense lightning for the EnergyGun.

#pragma once

#include "particles/ParticleTypes.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

/// @brief Continuous EnergyGun lightning built on the same ArcVertex path as Railgun.
class EnergyTeslaArcEffect
{
public:
    struct PathSample
    {
        std::array<glm::vec3, 33> points{};
        uint32_t count = 0;
    };

    void drive(uint32_t key,
               glm::vec3 origin,
               glm::vec3 guidePoint,
               glm::vec3 hitPoint,
               bool locked,
               float lockStrength);
    void debugPreview(glm::vec3 origin, glm::vec3 guidePoint, glm::vec3 hitPoint, bool locked, float lockStrength);
    void debugPulse(glm::vec3 origin, glm::vec3 guidePoint, glm::vec3 hitPoint, bool locked, float lockStrength);
    void update(float dt, glm::vec3 camForward);

    [[nodiscard]] const ArcVertex* mainArcData() const { return mainArcVerts_.data(); }
    [[nodiscard]] uint32_t mainArcCount() const { return static_cast<uint32_t>(mainArcVerts_.size()); }
    [[nodiscard]] const ArcVertex* detailArcData() const { return detailArcVerts_.data(); }
    [[nodiscard]] uint32_t detailArcCount() const { return static_cast<uint32_t>(detailArcVerts_.size()); }
    [[nodiscard]] uint32_t arcCount() const { return mainArcCount() + detailArcCount(); }
    [[nodiscard]] uint32_t activeBeamCount() const;

    [[nodiscard]] static PathSample buildGuidePathForTest(glm::vec3 origin,
                                                          glm::vec3 guidePoint,
                                                          glm::vec3 hitPoint,
                                                          bool locked,
                                                          float lockStrength);

private:
    static constexpr int k_maxBeams = 8;
    static constexpr int k_mainSegs = 32;
    static constexpr int k_branchSegs = 7;
    static constexpr int k_maxBranches = 8;
    static constexpr int k_maxStrands = 3;
    static constexpr float k_fadeTime = 0.08f;
    static constexpr float k_branchRetime = 0.045f;

    struct Branch
    {
        float tStart = 0.0f;
        float length = 0.0f;
        float angle = 0.0f;
        float side = 0.0f;
        float seed = 0.0f;
    };

    struct Beam
    {
        uint32_t key = 0;
        bool active = false;
        bool locked = false;
        glm::vec3 origin{0.0f};
        glm::vec3 guidePoint{0.0f};
        glm::vec3 hitPoint{0.0f};
        float lockStrength = 0.0f;
        float displayedLock = 0.0f;
        float age = 0.0f;
        float time = 0.0f;
        float seed = 0.0f;
        float warpSeed = 0.0f;
        float branchTimer = 0.0f;
        bool drivenThisFrame = false;
        std::array<Branch, k_maxBranches> branches{};
        int branchCount = 0;
    };

    std::array<Beam, k_maxBeams> beams_{};
    std::vector<ArcVertex> mainArcVerts_;
    std::vector<ArcVertex> detailArcVerts_;
    uint32_t debugPreviewKey_ = 0xEA10u;
    uint32_t debugKey_ = 0xEA11u;

    [[nodiscard]] static float hash01(uint32_t n);
    [[nodiscard]] static float smooth01(float v);
    [[nodiscard]] static glm::vec3 safeNormalize(glm::vec3 v, glm::vec3 fallback);
    [[nodiscard]] static glm::vec3 cubic(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t);
    [[nodiscard]] static PathSample buildGuidePath(glm::vec3 origin,
                                                   glm::vec3 guidePoint,
                                                   glm::vec3 hitPoint,
                                                   bool locked,
                                                   float lockStrength);

    Beam* findOrAllocBeam(uint32_t key);
    void rerandomizeBranches(Beam& beam);
    void appendArcStrip(std::vector<ArcVertex>& out,
                        const std::vector<glm::vec3>& pts,
                        float radius,
                        glm::vec4 color,
                        glm::vec3 camForward);
    void appendLayeredBolt(const std::vector<glm::vec3>& pts,
                           float fade,
                           float radiusScale,
                           bool primary,
                           glm::vec3 camForward);
    void appendBranches(const Beam& beam,
                        const std::vector<glm::vec3>& mainPts,
                        glm::vec3 axisN,
                        glm::vec3 perp,
                        glm::vec3 perp2,
                        float len,
                        float fade,
                        glm::vec3 camForward);
    void appendCorona(glm::vec3 center,
                      glm::vec3 axisN,
                      glm::vec3 perp,
                      glm::vec3 perp2,
                      float len,
                      float fade,
                      float seed,
                      bool targetCorona,
                      glm::vec3 camForward);
};
