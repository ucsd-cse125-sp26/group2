/// @file TeslaBeamEffect.cpp
/// @brief Implementation of the sustained, curved Tesla-Cannon energy arc.

#include "TeslaBeamEffect.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <glm/gtc/constants.hpp>

// Noise helpers (1-D value-noise fBm) — same family as HitscanEffect.

namespace
{

float randf()
{
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

float hash1(int n)
{
    uint32_t x = static_cast<uint32_t>(n);
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = (x >> 16) ^ x;
    return static_cast<float>(x & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

float vnoise(float t)
{
    const int i = static_cast<int>(std::floor(t));
    const float f = t - static_cast<float>(i);
    const float s = f * f * (3.f - 2.f * f); // smoothstep
    return glm::mix(hash1(i), hash1(i + 1), s);
}

float fbm(float t, float seed, float time, int octaves = 4)
{
    float d = 0.f;
    float amp = 1.f;
    float freq = 1.f;
    float tRate = 0.5f;
    for (int i = 0; i < octaves; ++i) {
        d += amp * (vnoise(t * freq + seed + time * tRate) * 2.f - 1.f);
        amp *= 0.40f;
        freq *= 3.00f;
        tRate *= 2.0f;
        seed += 13.37f;
    }
    return d;
}

float wfbm(float t, float seed, float warpSeed, float time, int octaves = 4)
{
    constexpr float kWarpStr = 0.14f;
    constexpr float kWarpFreq = 2.5f;
    const float tWarped = t + kWarpStr * (vnoise(t * kWarpFreq + warpSeed + time * 0.25f) * 2.f - 1.f);
    return fbm(tWarped, seed, time, octaves);
}

} // namespace

// Bezier helpers

glm::vec3 TeslaBeamEffect::evalBezier(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t)
{
    const float u = 1.f - t;
    return (u * u * u) * p0 + (3.f * u * u * t) * p1 + (3.f * u * t * t) * p2 + (t * t * t) * p3;
}

glm::vec3 TeslaBeamEffect::evalBezierTangent(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t)
{
    const float u = 1.f - t;
    return 3.f * u * u * (p1 - p0) + 6.f * u * t * (p2 - p1) + 3.f * t * t * (p3 - p2);
}

void TeslaBeamEffect::rerandomizeBranches(Beam& beam)
{
    beam.branchCount = 1 + (std::rand() % k_maxBranches); // 1-3
    for (int i = 0; i < beam.branchCount; ++i) {
        auto& b = beam.branches[i];
        b.tStart = 0.15f + randf() * 0.70f;
        b.length = 0.04f + randf() * 0.10f;
        b.angle = (randf() - 0.5f) * glm::radians(40.f);
        b.seed = randf() * 50.f;
    }
}

// Triangle-strip ribbon builder (camera-facing). Mirrors HitscanEffect.

void TeslaBeamEffect::appendArcStrip(const std::vector<glm::vec3>& pts,
                                     float radius,
                                     glm::vec4 color,
                                     glm::vec3 camForward)
{
    if (pts.size() < 2)
        return;

    std::vector<glm::vec3> sides;
    sides.reserve(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
        glm::vec3 tang;
        if (i == 0)
            tang = glm::normalize(pts[1] - pts[0]);
        else if (i + 1 == pts.size())
            tang = glm::normalize(pts[i] - pts[i - 1]);
        else
            tang = glm::normalize(pts[i + 1] - pts[i - 1]);

        glm::vec3 side = glm::cross(tang, camForward);
        if (glm::length(side) < 0.001f)
            side = glm::cross(tang, glm::vec3{0, 1, 0});
        sides.push_back(glm::normalize(side) * radius);
    }

    // Degenerate join from any previous strip so everything stays one draw call.
    if (!arcVerts_.empty()) {
        arcVerts_.push_back(arcVerts_.back());
        arcVerts_.push_back({pts[0] - sides[0], 1.f, color});
    }

    for (size_t i = 0; i < pts.size(); ++i) {
        arcVerts_.push_back({pts[i] - sides[i], 1.f, color});
        arcVerts_.push_back({pts[i] + sides[i], -1.f, color});
    }
}

// Beam slot management

TeslaBeamEffect::Beam* TeslaBeamEffect::findOrAllocBeam(uint32_t key)
{
    // Existing beam for this owner?
    for (auto& b : beams_) {
        if (b.active && b.key == key)
            return &b;
    }
    // Free slot, else steal the one closest to expiry.
    Beam* slot = nullptr;
    float oldest = FLT_MAX;
    for (auto& b : beams_) {
        if (!b.active)
            return &b;
        if (b.keepAlive < oldest) {
            oldest = b.keepAlive;
            slot = &b;
        }
    }
    return slot;
}

void TeslaBeamEffect::drive(uint32_t key, glm::vec3 origin, glm::vec3 target)
{
    Beam* slot = findOrAllocBeam(key);
    if (slot == nullptr)
        return;

    const bool isNew = !slot->active || slot->key != key;
    if (isNew) {
        slot->key = key;
        slot->active = true;
        slot->age = 0.f;
        slot->time = 0.f;
        slot->seed = randf() * 73.f;
        slot->warpSeed = randf() * 91.f;
        slot->branchTimer = 0.f;
        rerandomizeBranches(*slot);
    }
    slot->origin = origin;
    slot->target = target;
    slot->keepAlive = k_keepAlive; // refreshed every frame it keeps firing
}

void TeslaBeamEffect::update(float dt, glm::vec3 camForward)
{
    arcVerts_.clear();

    for (auto& beam : beams_) {
        if (!beam.active)
            continue;

        // Age out beams that stopped being driven.
        beam.keepAlive -= dt;
        if (beam.keepAlive <= -k_fadeTime) {
            beam.active = false;
            continue;
        }

        beam.age += dt;
        beam.time += dt;

        beam.branchTimer += dt;
        if (beam.branchTimer >= k_branchRetime) {
            beam.branchTimer -= k_branchRetime;
            rerandomizeBranches(beam);
        }

        const glm::vec3 axis = beam.target - beam.origin;
        const float len = glm::length(axis);
        if (len < 0.001f)
            continue;
        const glm::vec3 axisN = axis / len;

        // Sag direction: world-down projected perpendicular to the beam, so the
        // arc bows downward toward the target like Winston's tesla cannon. If
        // the beam is near-vertical, fall back to the camera-facing perpendicular.
        const glm::vec3 worldDown{0.f, -1.f, 0.f};
        glm::vec3 sag = worldDown - axisN * glm::dot(worldDown, axisN);
        if (glm::length(sag) < 0.05f)
            sag = glm::cross(axisN, camForward);
        if (glm::length(sag) < 0.05f)
            sag = glm::cross(axisN, glm::vec3{1, 0, 0});
        sag = glm::normalize(sag);

        // Camera-facing perpendicular for the fBm crackle + a little live wobble.
        glm::vec3 perp = glm::cross(axisN, camForward);
        if (glm::length(perp) < 0.01f)
            perp = glm::cross(axisN, glm::vec3{0, 1, 0});
        perp = glm::normalize(perp);
        const glm::vec3 perp2 = glm::normalize(glm::cross(axisN, perp));

        // Deeply-bowed control points → a smooth, obvious curve to the target.
        const float bow = len * k_bowFrac;
        const float wobble = std::sin(beam.time * 9.f) * len * 0.015f;
        const glm::vec3 cp1 = beam.origin + axis * 0.33f + sag * bow + perp * wobble;
        const glm::vec3 cp2 = beam.origin + axis * 0.66f + sag * bow - perp * wobble;

        // Fade: ramp in over k_fadeTime, ramp out once keepAlive goes negative.
        const float fadeIn = std::min(1.f, beam.age / k_fadeTime);
        const float fadeOut = std::min(1.f, std::max(0.f, (beam.keepAlive + k_fadeTime) / k_fadeTime));
        const float fade = fadeIn * fadeOut;

        // crackle amplitude — small relative to the macro bow so the curve reads.
        const float baseAmp = len * 0.012f;

        std::vector<glm::vec3> mainPts;
        mainPts.reserve(k_bezierSegs + 1);
        for (int i = 0; i <= k_bezierSegs; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(k_bezierSegs);
            glm::vec3 pt = evalBezier(beam.origin, cp1, cp2, beam.target, t);
            const float env = std::sin(t * glm::pi<float>());
            const float du = wfbm(t, beam.seed, beam.warpSeed, beam.time, 4);
            const float dv = wfbm(t, beam.seed + 33.f, beam.warpSeed + 19.f, beam.time, 3);
            pt += perp * (du * baseAmp * env) + perp2 * (dv * baseAmp * 0.4f * env);
            mainPts.push_back(pt);
        }

        // Energy-green palette (matches the beam point-light colour).
        appendArcStrip(mainPts, 3.2f, {0.20f, 0.85f, 0.30f, 0.12f * fade}, camForward);
        appendArcStrip(mainPts, 1.1f, {0.45f, 1.00f, 0.55f, 0.75f * fade}, camForward);
        appendArcStrip(mainPts, 0.5f, {0.90f, 1.00f, 0.92f, 1.00f * fade}, camForward);

        // Forked branches.
        for (int b = 0; b < beam.branchCount; ++b) {
            const auto& br = beam.branches[b];
            const int rootIdx =
                std::clamp(static_cast<int>(br.tStart * static_cast<float>(k_bezierSegs) + 0.5f), 0, k_bezierSegs);
            const glm::vec3 root = mainPts[static_cast<size_t>(rootIdx)];

            const glm::vec3 tang = evalBezierTangent(beam.origin, cp1, cp2, beam.target, br.tStart);
            const float tangL = glm::length(tang);
            const glm::vec3 tangN = (tangL > 0.001f) ? tang / tangL : axisN;
            const glm::vec3 brDir = glm::normalize(tangN * std::cos(br.angle) + perp * std::sin(br.angle));

            const float brLen = len * br.length;
            const glm::vec3 root2 = root + brDir * brLen;
            const glm::vec3 brAxis = root2 - root;

            std::vector<glm::vec3> brPts;
            brPts.reserve(k_branchSegs + 1);
            for (int j = 0; j <= k_branchSegs; ++j) {
                const float bt = static_cast<float>(j) / static_cast<float>(k_branchSegs);
                glm::vec3 bpt = root + brAxis * bt;
                const float benv = std::sin(bt * glm::pi<float>());
                const float du = fbm(bt, br.seed, beam.time * 1.8f, 2);
                bpt += perp * (du * brLen * 0.04f * benv);
                brPts.push_back(bpt);
            }

            appendArcStrip(brPts, 0.8f, {0.30f, 0.90f, 0.40f, 0.28f * fade}, camForward);
            appendArcStrip(brPts, 0.2f, {0.85f, 1.00f, 0.90f, 0.80f * fade}, camForward);
        }
    }
}

uint32_t TeslaBeamEffect::activeBeamCount() const
{
    uint32_t c = 0;
    for (const auto& b : beams_)
        c += b.active ? 1u : 0u;
    return c;
}
