/// @file EnergyTeslaArcEffect.cpp
/// @brief Implementation of sustained Railgun-style EnergyGun lightning.

#include "EnergyTeslaArcEffect.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace
{

float hash01Local(uint32_t n)
{
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return static_cast<float>(n & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float vnoise(float t, float seed)
{
    const int i = static_cast<int>(std::floor(t));
    const float f = t - static_cast<float>(i);
    const float s = f * f * (3.0f - 2.0f * f);
    const uint32_t seedBits = static_cast<uint32_t>(seed * 4096.0f);
    return glm::mix(hash01Local(static_cast<uint32_t>(i) * 747796405u + seedBits),
                    hash01Local(static_cast<uint32_t>(i + 1) * 747796405u + seedBits),
                    s);
}

float fbm(float t, float seed, float time, int octaves = 4)
{
    float d = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    float tRate = 0.5f;
    for (int i = 0; i < octaves; ++i) {
        d += amp * (vnoise(t * freq + time * tRate, seed) * 2.0f - 1.0f);
        amp *= 0.40f;
        freq *= 3.0f;
        tRate *= 2.0f;
        seed += 13.37f;
    }
    return d;
}

float wfbm(float t, float seed, float warpSeed, float time, int octaves = 4)
{
    const float warped = t + 0.14f * (vnoise(t * 2.5f + time * 0.25f, warpSeed) * 2.0f - 1.0f);
    return fbm(warped, seed, time, octaves);
}

float signedHash(uint32_t n)
{
    return hash01Local(n) * 2.0f - 1.0f;
}

} // namespace

float EnergyTeslaArcEffect::hash01(uint32_t n)
{
    return hash01Local(n);
}

float EnergyTeslaArcEffect::smooth01(float v)
{
    v = std::clamp(v, 0.0f, 1.0f);
    return v * v * (3.0f - 2.0f * v);
}

glm::vec3 EnergyTeslaArcEffect::safeNormalize(glm::vec3 v, glm::vec3 fallback)
{
    const float len = glm::length(v);
    if (len > 0.0001f)
        return v / len;
    const float fallbackLen = glm::length(fallback);
    return (fallbackLen > 0.0001f) ? fallback / fallbackLen : glm::vec3{0.0f, 0.0f, 1.0f};
}

glm::vec3 EnergyTeslaArcEffect::cubic(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t)
{
    const float u = 1.0f - t;
    return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
}

EnergyTeslaArcEffect::PathSample EnergyTeslaArcEffect::buildGuidePath(glm::vec3 origin,
                                                                      glm::vec3 guidePoint,
                                                                      glm::vec3 hitPoint,
                                                                      bool locked,
                                                                      float lockStrength)
{
    PathSample path{};
    path.count = static_cast<uint32_t>(k_mainSegs + 1);
    const glm::vec3 guideAxis = guidePoint - origin;
    const glm::vec3 guideN = safeNormalize(guideAxis, hitPoint - origin);
    const float guideLen = glm::max(glm::length(guideAxis), 1.0f);
    const float bend = locked ? smooth01(lockStrength) : 0.0f;
    constexpr float kBendStart = 0.58f;

    const glm::vec3 bendStart = origin + guideAxis * kBendStart;
    const glm::vec3 toTarget = hitPoint - bendStart;
    const float tailLen = glm::max(glm::length(toTarget), guideLen * 0.12f);
    const glm::vec3 targetN = safeNormalize(toTarget, guideN);
    const glm::vec3 cp1 = bendStart + guideN * tailLen * 0.36f;
    const glm::vec3 cp2 = hitPoint - targetN * tailLen * 0.24f;

    for (int i = 0; i <= k_mainSegs; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(k_mainSegs);
        const glm::vec3 straight = glm::mix(origin, guidePoint, t);
        glm::vec3 bent = straight;
        if (t > kBendStart) {
            const float u = (t - kBendStart) / (1.0f - kBendStart);
            bent = cubic(bendStart, cp1, cp2, hitPoint, u);
        }
        path.points[static_cast<size_t>(i)] = glm::mix(straight, bent, bend);
    }
    path.points.front() = origin;
    path.points[static_cast<size_t>(path.count - 1)] = glm::mix(guidePoint, hitPoint, bend);
    return path;
}

EnergyTeslaArcEffect::PathSample EnergyTeslaArcEffect::buildGuidePathForTest(glm::vec3 origin,
                                                                              glm::vec3 guidePoint,
                                                                              glm::vec3 hitPoint,
                                                                              bool locked,
                                                                              float lockStrength)
{
    return buildGuidePath(origin, guidePoint, hitPoint, locked, lockStrength);
}

EnergyTeslaArcEffect::Beam* EnergyTeslaArcEffect::findOrAllocBeam(uint32_t key)
{
    for (auto& beam : beams_) {
        if (beam.active && beam.key == key)
            return &beam;
    }
    Beam* slot = nullptr;
    float oldestAge = -FLT_MAX;
    for (auto& beam : beams_) {
        if (!beam.active)
            return &beam;
        if (beam.age > oldestAge) {
            oldestAge = beam.age;
            slot = &beam;
        }
    }
    return slot;
}

void EnergyTeslaArcEffect::rerandomizeBranches(Beam& beam)
{
    const uint32_t base = static_cast<uint32_t>(beam.seed * 4096.0f) ^
                          static_cast<uint32_t>(std::floor(beam.time / k_branchRetime)) * 2654435761u;
    beam.branchCount = beam.locked ? k_maxBranches : 4;
    for (int i = 0; i < beam.branchCount; ++i) {
        const float r0 = hash01(base + static_cast<uint32_t>(i) * 101u);
        const float r1 = hash01(base + static_cast<uint32_t>(i) * 223u);
        const float r2 = hash01(base + static_cast<uint32_t>(i) * 397u);
        const float r3 = hash01(base + static_cast<uint32_t>(i) * 631u);
        auto& branch = beam.branches[static_cast<size_t>(i)];
        branch.tStart = beam.locked ? (0.55f + r0 * 0.40f) : (0.12f + r0 * 0.72f);
        branch.length = beam.locked ? (0.045f + r1 * 0.105f) : (0.025f + r1 * 0.060f);
        branch.angle = (r2 - 0.5f) * glm::radians(32.0f);
        branch.side = r3 * 2.0f - 1.0f;
        branch.seed = 9.0f + r0 * 73.0f;
    }
}

void EnergyTeslaArcEffect::drive(uint32_t key,
                                 glm::vec3 origin,
                                 glm::vec3 guidePoint,
                                 glm::vec3 hitPoint,
                                 bool locked,
                                 float lockStrength)
{
    Beam* beam = findOrAllocBeam(key);
    if (!beam)
        return;

    const bool fresh = !beam->active || beam->key != key;
    if (fresh) {
        *beam = Beam{};
        beam->active = true;
        beam->key = key;
        beam->seed = 17.0f + hash01(key * 1664525u + 1013904223u) * 83.0f;
        beam->warpSeed = 31.0f + hash01(key * 22695477u + 1u) * 97.0f;
    }
    beam->origin = origin;
    beam->guidePoint = guidePoint;
    beam->hitPoint = hitPoint;
    beam->locked = locked;
    beam->lockStrength = std::clamp(lockStrength, 0.0f, 1.0f);
    beam->drivenThisFrame = true;
}

void EnergyTeslaArcEffect::debugPulse(glm::vec3 origin,
                                      glm::vec3 guidePoint,
                                      glm::vec3 hitPoint,
                                      bool locked,
                                      float lockStrength)
{
    drive(debugKey_++, origin, guidePoint, hitPoint, locked, lockStrength);
}

void EnergyTeslaArcEffect::appendArcStrip(std::vector<ArcVertex>& out,
                                          const std::vector<glm::vec3>& pts,
                                          float radius,
                                          glm::vec4 color,
                                          glm::vec3 camForward)
{
    if (pts.size() < 2)
        return;

    std::vector<glm::vec3> sides;
    sides.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        glm::vec3 tangent;
        if (i == 0)
            tangent = pts[1] - pts[0];
        else if (i + 1 == pts.size())
            tangent = pts[i] - pts[i - 1];
        else
            tangent = pts[i + 1] - pts[i - 1];

        tangent = safeNormalize(tangent, {0.0f, 0.0f, 1.0f});
        glm::vec3 side = glm::cross(tangent, camForward);
        if (glm::length(side) < 0.001f)
            side = glm::cross(tangent, {0.0f, 1.0f, 0.0f});
        sides.push_back(safeNormalize(side, {1.0f, 0.0f, 0.0f}) * radius);
    }

    if (!out.empty()) {
        out.push_back(out.back());
        out.push_back({pts[0] - sides[0], 1.0f, color});
    }
    for (size_t i = 0; i < pts.size(); ++i) {
        out.push_back({pts[i] - sides[i], 1.0f, color});
        out.push_back({pts[i] + sides[i], -1.0f, color});
    }
}

void EnergyTeslaArcEffect::appendLayeredBolt(const std::vector<glm::vec3>& pts,
                                             float fade,
                                             float radiusScale,
                                             bool primary,
                                             glm::vec3 camForward)
{
    std::vector<ArcVertex>& out = primary ? mainArcVerts_ : detailArcVerts_;
    appendArcStrip(out, pts, 2.05f * radiusScale, {0.16f, 0.70f, 1.00f, 0.055f * fade}, camForward);
    appendArcStrip(out, pts, 0.96f * radiusScale, {0.46f, 0.90f, 1.00f, 0.72f * fade}, camForward);
    appendArcStrip(out, pts, 0.40f * radiusScale, {0.96f, 0.99f, 1.00f, 1.00f * fade}, camForward);
}

void EnergyTeslaArcEffect::appendBranches(const Beam& beam,
                                          const std::vector<glm::vec3>& mainPts,
                                          glm::vec3 axisN,
                                          glm::vec3 perp,
                                          glm::vec3 perp2,
                                          float len,
                                          float fade,
                                          glm::vec3 camForward)
{
    for (int i = 0; i < beam.branchCount; ++i) {
        const auto& branch = beam.branches[static_cast<size_t>(i)];
        const int rootIdx =
            std::clamp(static_cast<int>(branch.tStart * static_cast<float>(mainPts.size() - 1) + 0.5f),
                       0,
                       static_cast<int>(mainPts.size() - 1));
        const glm::vec3 root = mainPts[static_cast<size_t>(rootIdx)];
        const float branchLen = len * branch.length * (beam.locked ? (0.8f + beam.displayedLock * 0.65f) : 0.7f);
        const glm::vec3 dir = safeNormalize(axisN * 0.34f + perp * branch.side + perp2 * std::sin(branch.angle), perp);
        std::vector<glm::vec3> pts;
        pts.reserve(k_branchSegs + 1);
        for (int j = 0; j <= k_branchSegs; ++j) {
            const float t = static_cast<float>(j) / static_cast<float>(k_branchSegs);
            const float env = std::sin(t * glm::pi<float>());
            glm::vec3 p = root + dir * branchLen * t;
            p += perp * (fbm(t, branch.seed, beam.time * 1.8f, 2) * branchLen * 0.045f * env);
            p += perp2 * (fbm(t, branch.seed + 7.3f, beam.time * 1.8f, 2) * branchLen * 0.025f * env);
            pts.push_back(p);
        }
        appendArcStrip(detailArcVerts_, pts, 0.54f, {0.28f, 0.82f, 1.00f, 0.20f * fade}, camForward);
        appendArcStrip(detailArcVerts_, pts, 0.24f, {0.90f, 0.98f, 1.00f, 0.82f * fade}, camForward);
    }
}

void EnergyTeslaArcEffect::appendCorona(glm::vec3 center,
                                        glm::vec3 axisN,
                                        glm::vec3 perp,
                                        glm::vec3 perp2,
                                        float len,
                                        float fade,
                                        float seed,
                                        bool targetCorona,
                                        glm::vec3 camForward)
{
    const int count = targetCorona ? 7 : 4;
    const float coronaLen = len * (targetCorona ? 0.032f : 0.018f);
    for (int i = 0; i < count; ++i) {
        const float a = seed + static_cast<float>(i) * glm::two_pi<float>() / static_cast<float>(count);
        const glm::vec3 radial = safeNormalize(perp * std::cos(a) + perp2 * std::sin(a), perp);
        std::vector<glm::vec3> pts;
        pts.push_back(center - axisN * coronaLen * 0.10f);
        pts.push_back(center + radial * coronaLen * 0.45f + axisN * coronaLen * 0.10f);
        pts.push_back(center + radial * coronaLen * (0.75f + 0.35f * hash01(static_cast<uint32_t>(seed * 1000.0f) + i)));
        appendArcStrip(detailArcVerts_, pts, targetCorona ? 0.32f : 0.22f, {0.24f, 0.90f, 1.0f, 0.24f * fade},
                       camForward);
        appendArcStrip(detailArcVerts_, pts, targetCorona ? 0.16f : 0.10f, {0.94f, 1.0f, 1.0f, 0.90f * fade},
                       camForward);
    }
}

void EnergyTeslaArcEffect::update(float dt, glm::vec3 camForward)
{
    mainArcVerts_.clear();
    detailArcVerts_.clear();

    for (auto& beam : beams_) {
        if (!beam.active)
            continue;

        if (!beam.drivenThisFrame) {
            beam.active = false;
            continue;
        }
        beam.drivenThisFrame = false;

        beam.age += dt;
        beam.time += dt;
        beam.displayedLock = glm::mix(beam.displayedLock, beam.locked ? beam.lockStrength : 0.0f,
                                      1.0f - std::exp(-dt * 11.0f));
        beam.branchTimer += dt;
        if (beam.branchTimer >= k_branchRetime) {
            beam.branchTimer = std::fmod(beam.branchTimer, k_branchRetime);
            rerandomizeBranches(beam);
        }

        const PathSample path = buildGuidePath(beam.origin, beam.guidePoint, beam.hitPoint, beam.locked, beam.displayedLock);
        const glm::vec3 axis = beam.guidePoint - beam.origin;
        const float len = glm::length(axis);
        if (len < 0.5f)
            continue;
        const glm::vec3 axisN = safeNormalize(axis, beam.hitPoint - beam.origin);
        glm::vec3 perp = glm::cross(axisN, camForward);
        if (glm::length(perp) < 0.01f)
            perp = glm::cross(axisN, {0.0f, 1.0f, 0.0f});
        perp = safeNormalize(perp, {1.0f, 0.0f, 0.0f});
        const glm::vec3 perp2 = safeNormalize(glm::cross(axisN, perp), {0.0f, 1.0f, 0.0f});

        const float fadeIn = std::min(1.0f, beam.age / k_fadeTime);
        const float fade = fadeIn;
        const float baseAmp = len * (beam.locked ? (0.010f + beam.displayedLock * 0.018f) : 0.008f);
        const int strandCount = beam.locked ? 1 : k_maxStrands;
        const uint32_t crackleFrame = static_cast<uint32_t>(std::floor(beam.time * 34.0f));
        const uint32_t sparkFrame = static_cast<uint32_t>(std::floor(beam.time * 71.0f));

        std::vector<glm::vec3> primaryPts;
        primaryPts.reserve(k_mainSegs + 1);

        for (int strand = 0; strand < strandCount; ++strand) {
            std::vector<glm::vec3> pts;
            pts.reserve(k_mainSegs + 1);
            const float strandOffset = (static_cast<float>(strand) - 1.0f) * len * 0.012f;
            const float strandSeed = beam.seed + static_cast<float>(strand) * 19.0f;
            for (uint32_t i = 0; i < path.count; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(path.count - 1);
                const float env = std::pow(std::sin(t * glm::pi<float>()), 0.72f);
                const float tailBoost = 1.0f + beam.displayedLock * smooth01((t - 0.60f) / 0.40f) * 1.35f;
                const uint32_t seedBits = static_cast<uint32_t>(strandSeed * 4096.0f);
                const uint32_t cell = seedBits ^ (i * 2246822519u) ^ (crackleFrame * 3266489917u);
                const uint32_t sparkCell = seedBits ^ (i * 668265263u) ^ (sparkFrame * 374761393u);
                const float zigzag = ((i & 1u) != 0u ? 1.0f : -1.0f) * (0.45f + hash01(cell + 17u) * 0.95f);
                const float hardStepU = signedHash(cell + 101u) * 1.25f + zigzag;
                const float hardStepV = signedHash(cell + 211u) * 0.92f +
                                        ((i % 3u) == 0u ? signedHash(sparkCell + 37u) * 0.85f : 0.0f);
                const float waveU = std::sin(t * 46.0f + beam.time * 42.0f + strandSeed) * 0.55f;
                const float waveV = std::sin(t * 31.0f - beam.time * 35.0f + strandSeed * 0.73f) * 0.36f;
                glm::vec3 p = path.points[i];
                p += perp * strandOffset * env;
                p += perp * ((hardStepU + waveU + wfbm(t, strandSeed, beam.warpSeed, beam.time, 3) * 0.42f) *
                             baseAmp * env * tailBoost);
                p += perp2 * ((hardStepV + waveV + wfbm(t, strandSeed + 33.0f, beam.warpSeed + 19.0f, beam.time, 2) *
                                           0.35f) *
                              baseAmp * 0.58f * env * tailBoost);
                pts.push_back(p);
            }
            appendLayeredBolt(pts, fade * (strand == 0 ? 1.0f : 0.56f), strand == 0 ? 1.0f : 0.72f, strand == 0,
                              camForward);
            if (strand == 0)
                primaryPts = pts;
        }

        if (!primaryPts.empty()) {
            appendBranches(beam, primaryPts, axisN, perp, perp2, len, fade, camForward);
            appendCorona(primaryPts.front(), axisN, perp, perp2, len, fade * 0.65f, beam.seed + beam.time, false,
                         camForward);
            appendCorona(primaryPts.back(), axisN, perp, perp2, len,
                         fade * (beam.locked ? (0.75f + beam.displayedLock * 0.45f) : 0.30f),
                         beam.seed + beam.time * 1.7f,
                         true,
                         camForward);
        }
    }
}

uint32_t EnergyTeslaArcEffect::activeBeamCount() const
{
    uint32_t count = 0;
    for (const auto& beam : beams_)
        count += beam.active ? 1u : 0u;
    return count;
}
