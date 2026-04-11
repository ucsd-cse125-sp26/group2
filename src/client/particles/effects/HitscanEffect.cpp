/// @file HitscanEffect.cpp
/// @brief Implementation of hitscan energy beam with fBm path deviation.

#include "HitscanEffect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

/// @brief Return a random float in [0, 1].
static float randf()
{
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}

// 1-D smooth value noise + fBm
//
// Using integer hash -> cubic-interpolated value noise.
// The noise is sampled at (t * freq + seed + time * animRate), so:
//   - freq controls how rapidly the bolt wiggles spatially
//   - animRate controls how fast that octave changes over time

/// @brief Good avalanche hash -- gives uniform [0,1] output for any integer input.
/// @param n Integer input.
/// @return Hashed value in [0, 1].
static float hash1(int n)
{
    uint32_t x = static_cast<uint32_t>(n);
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = (x >> 16) ^ x;
    return static_cast<float>(x & 0xFFFFFFu) / static_cast<float>(0xFFFFFFu);
}

/// @brief 1-D value noise with cubic (smoothstep) interpolation.  Continuous in t.
/// @param t Sample position along the noise domain.
/// @return Noise value in [0, 1].
static float vnoise(float t)
{
    const int i = static_cast<int>(std::floor(t));
    const float f = t - static_cast<float>(i);
    const float s = f * f * (3.f - 2.f * f); // smoothstep
    return glm::mix(hash1(i), hash1(i + 1), s);
}

/// @brief 4-octave fBm displacement (lacunarity ~ 3, persistence ~ 0.4).
///
/// Higher octaves run at 2x the animation speed of the one below them,
/// giving slow large-scale breathing overlaid with fast fine crackle.
/// @param t       Sample position along the arc [0, 1].
/// @param seed    Base seed for this noise instance.
/// @param time    Elapsed animation time (drives temporal evolution).
/// @param octaves Number of fBm octaves to sum.
/// @return Displacement value in approximately [-1, +1].
static float fbm(float t, float seed, float time, int octaves = 4)
{
    float d = 0.f;
    float amp = 1.f;
    float freq = 1.f;
    float tRate = 0.5f; // animation speed per octave (doubles each level)
    for (int i = 0; i < octaves; ++i) {
        d += amp * (vnoise(t * freq + seed + time * tRate) * 2.f - 1.f);
        amp *= 0.40f;   // persistence
        freq *= 3.00f;  // lacunarity
        tRate *= 2.0f;
        seed += 13.37f; // independent phase offset per octave
    }
    return d;
}

/// @brief Domain-warped fBm: warp t before sampling.
///
/// Warping causes frequency content to vary spatially -- some regions are smooth,
/// others suddenly jagged -- matching how real lightning looks.
/// @param t       Sample position along the arc [0, 1].
/// @param seed    Base fBm seed.
/// @param warpSeed Seed for the domain warp noise.
/// @param time    Elapsed animation time.
/// @param octaves Number of fBm octaves to sum.
/// @return Warped displacement value in approximately [-1, +1].
static float wfbm(float t, float seed, float warpSeed, float time, int octaves = 4)
{
    constexpr float kWarpStr = 0.14f;
    constexpr float kWarpFreq = 2.5f;
    const float tWarped = t + kWarpStr * (vnoise(t * kWarpFreq + warpSeed + time * 0.25f) * 2.f - 1.f);
    return fbm(tWarped, seed, time, octaves);
}

// Bezier helpers

glm::vec3 HitscanEffect::evalBezier(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t)
{
    const float u = 1.f - t;
    return (u * u * u) * p0 + (3.f * u * u * t) * p1 + (3.f * u * t * t) * p2 + (t * t * t) * p3;
}

glm::vec3 HitscanEffect::evalBezierTangent(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t)
{
    const float u = 1.f - t;
    return 3.f * u * u * (p1 - p0) + 6.f * u * t * (p2 - p1) + 3.f * t * t * (p3 - p2);
}

// Control-point + branch randomization

void HitscanEffect::randomizeCP(
    glm::vec3 origin, glm::vec3 hitPos, glm::vec3 camForward, glm::vec3& cp1, glm::vec3& cp2)
{
    const glm::vec3 axis = hitPos - origin;
    const float len = glm::length(axis);
    const glm::vec3 axisN = (len > 0.001f) ? axis / len : glm::vec3{0, 0, 1};

    // Primary perpendicular: in the camera-facing plane
    glm::vec3 perp = glm::cross(axisN, camForward);
    if (glm::length(perp) < 0.01f)
        perp = glm::cross(axisN, glm::vec3{0, 1, 0});
    perp = glm::normalize(perp);

    // Secondary perpendicular for out-of-plane curvature
    const glm::vec3 perp2 = glm::normalize(glm::cross(axisN, perp));

    const float maxBulge = len * 0.22f;
    cp1 = origin + axis * 0.30f + perp * ((randf() * 2.f - 1.f) * maxBulge) +
          perp2 * ((randf() * 2.f - 1.f) * maxBulge * 0.40f);
    cp2 = hitPos - axis * 0.30f + perp * ((randf() * 2.f - 1.f) * maxBulge) +
          perp2 * ((randf() * 2.f - 1.f) * maxBulge * 0.40f);
}

void HitscanEffect::rerandomizeBranches(BezierBeam& beam, glm::vec3 /*camForward*/)
{
    beam.branchCount = 2 + (std::rand() % (k_maxBranches - 1)); // 2-5

    for (int i = 0; i < beam.branchCount; ++i) {
        auto& b = beam.branches[i];
        b.tStart = 0.10f + randf() * 0.75f;              // spread over the bolt body
        b.length = 0.07f + randf() * 0.17f;              // 7-24 % of main bolt length
        b.angle = (randf() - 0.5f) * glm::radians(60.f); // +/-30 deg divergence
        b.seed = randf() * 50.f;
    }
}

// Triangle-strip ribbon builder
//
// Each segment of the path gets two vertices (left edge=+1, right edge=-1).
// When appending to a non-empty strip, two degenerate vertices are prepended
// to restart the strip -- this keeps everything in a single draw call.

void HitscanEffect::appendArcStrip(const std::vector<glm::vec3>& pts,
                                   float radius,
                                   glm::vec4 color,
                                   glm::vec3 camForward)
{
    if (pts.size() < 2)
        return;

    // Pre-compute camera-facing side vectors at each sample
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

    // Degenerate join from previous strip segment
    if (!arcVerts_.empty()) {
        arcVerts_.push_back(arcVerts_.back());
        arcVerts_.push_back({pts[0] - sides[0], 1.f, color});
    }

    for (size_t i = 0; i < pts.size(); ++i) {
        arcVerts_.push_back({pts[i] - sides[i], 1.f, color});
        arcVerts_.push_back({pts[i] + sides[i], -1.f, color});
    }
}

// spawn

void HitscanEffect::spawn(glm::vec3 origin, glm::vec3 hitPos, WeaponType /*wt*/, glm::vec3 camForward)
{
    // Find a free slot; fall back to stealing the one closest to expiry
    BezierBeam* slot = nullptr;
    float oldest = FLT_MAX;
    for (auto& b : beams_) {
        if (!b.active) {
            slot = &b;
            break;
        }
        if (b.lifetime < oldest) {
            oldest = b.lifetime;
            slot = &b;
        }
    }

    if (slot->active) {
        // Capture current interpolated shape as the morph-from state
        slot->cp1Prev = glm::mix(slot->cp1Prev, slot->cp1Curr, slot->interpT);
        slot->cp2Prev = glm::mix(slot->cp2Prev, slot->cp2Curr, slot->interpT);
    } else {
        // First shot ever: start from a plausible shape so interpT=0 isn't a
        // straight line jump
        randomizeCP(origin, hitPos, camForward, slot->cp1Prev, slot->cp2Prev);
    }

    slot->origin = origin;
    slot->hitPos = hitPos;
    slot->lifetime = k_beamLifetime;
    slot->active = true;
    slot->interpT = 0.f;
    slot->time = 0.f;
    slot->noiseSeed = randf() * 73.f;
    slot->warpSeed = randf() * 91.f;
    slot->branchTimer = 0.f;

    randomizeCP(origin, hitPos, camForward, slot->cp1Curr, slot->cp2Curr);
    rerandomizeBranches(*slot, camForward);

    // Schedule return strokes at 60 / 120 / 180 ms
    slot->returnStrokes[0] = {0.060f, randf() * 50.f, false};
    slot->returnStrokes[1] = {0.120f, randf() * 50.f, false};
    slot->returnStrokes[2] = {0.180f, randf() * 50.f, false};
}

// update -- rebuilds arcVerts_ every frame

void HitscanEffect::update(float dt, glm::vec3 camForward)
{
    arcVerts_.clear();

    for (auto& beam : beams_) {
        if (!beam.active)
            continue;

        beam.lifetime -= dt;
        if (beam.lifetime <= 0.f) {
            beam.active = false;
            continue;
        }

        beam.time += dt;
        beam.interpT = std::min(1.f, beam.interpT + dt / k_interpDuration);

        // Re-randomize branches at fixed interval for flicker
        beam.branchTimer += dt;
        if (beam.branchTimer >= BezierBeam::k_branchRetime) {
            beam.branchTimer -= BezierBeam::k_branchRetime;
            rerandomizeBranches(beam, camForward);
        }

        // Fire scheduled return strokes
        const float elapsed = k_beamLifetime - beam.lifetime;
        for (auto& rs : beam.returnStrokes) {
            if (!rs.fired && elapsed >= rs.fireAt) {
                rs.fired = true;
                // Capture current shape -> generate new arc path -> restart morph
                beam.cp1Prev = glm::mix(beam.cp1Prev, beam.cp1Curr, beam.interpT);
                beam.cp2Prev = glm::mix(beam.cp2Prev, beam.cp2Curr, beam.interpT);
                randomizeCP(beam.origin, beam.hitPos, camForward, beam.cp1Curr, beam.cp2Curr);
                beam.interpT = 0.f;
                beam.noiseSeed = rs.seed;
                beam.warpSeed = rs.seed * 1.37f;
                rerandomizeBranches(beam, camForward);
            }
        }

        // Bezier spine
        const glm::vec3 cp1 = glm::mix(beam.cp1Prev, beam.cp1Curr, beam.interpT);
        const glm::vec3 cp2 = glm::mix(beam.cp2Prev, beam.cp2Curr, beam.interpT);

        const glm::vec3 axis = beam.hitPos - beam.origin;
        const float len = glm::length(axis);
        if (len < 0.001f)
            continue;
        const glm::vec3 axisN = axis / len;

        // Perpendicular basis for fBm displacement
        glm::vec3 perp = glm::cross(axisN, camForward);
        if (glm::length(perp) < 0.01f)
            perp = glm::cross(axisN, glm::vec3{0, 1, 0});
        perp = glm::normalize(perp);
        const glm::vec3 perp2 = glm::normalize(glm::cross(axisN, perp));

        // baseAmp: 16 % of beam length -- controls overall path deviation
        const float baseAmp = len * 0.16f;

        // Fade envelope (ramp in 8 %, ramp out 25 %)
        const float tLife = elapsed / k_beamLifetime;
        const float rampIn = std::min(1.f, tLife / 0.08f);
        const float rampOut = std::min(1.f, beam.lifetime / (k_beamLifetime * 0.25f));
        const float fade = rampIn * rampOut;

        // Sample main arc
        // For each sample t in [0,1]:
        //   1. Evaluate Bezier spine point (large-scale arc shape, smooth morph)
        //   2. Domain-warp t, then sample 4-octave fBm in two perpendicular axes
        //   3. Multiply by sin(t*pi) envelope -- pinned at both endpoints
        //   4. Displace the spine point

        std::vector<glm::vec3> mainPts;
        mainPts.reserve(k_bezierSegs + 1);

        for (int i = 0; i <= k_bezierSegs; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(k_bezierSegs);
            glm::vec3 pt = evalBezier(beam.origin, cp1, cp2, beam.hitPos, t);

            // Envelope: sin(t*pi) peaks at midpoint, zero at both endpoints
            const float env = std::sin(t * glm::pi<float>());

            // fBm along U-axis (primary wiggle plane)
            const float du = wfbm(t, beam.noiseSeed, beam.warpSeed, beam.time, 4);
            // fBm along V-axis (secondary, 3 octaves only, smaller amplitude)
            const float dv = wfbm(t, beam.noiseSeed + 33.f, beam.warpSeed + 19.f, beam.time, 3);

            pt += perp * (du * baseAmp * env) + perp2 * (dv * baseAmp * 0.45f * env);
            mainPts.push_back(pt);
        }

        // Three-layer main arc render
        //
        // The three layers with different radii and alphas produce the HDR look:
        // the wide-low-alpha bloom layer makes the whole arc glow across a large
        // screen area; the tight high-alpha core looks white-hot.

        // Layer 1 -- outer bloom halo  (wide, dim -- simulates atmospheric scatter)
        appendArcStrip(mainPts, 6.5f, {0.20f, 0.50f, 1.00f, 0.07f * fade}, camForward);

        // Layer 2 -- inner energy channel  (the main visible arc body)
        appendArcStrip(mainPts, 1.8f, {0.45f, 0.80f, 1.00f, 0.55f * fade}, camForward);

        // Layer 3 -- white-hot centerline  (overdriven -- simulates HDR core)
        appendArcStrip(mainPts, 0.40f, {0.92f, 0.97f, 1.00f, 0.96f * fade}, camForward);

        // Branches
        //
        // Each branch starts at a clamped point on the main arc, extends at an
        // angle from the tangent, and uses 2-octave fBm for its own path.
        // Branches are visibly thinner and dimmer than the main arc.

        for (int b = 0; b < beam.branchCount; ++b) {
            const auto& br = beam.branches[b];

            // Root point: sample main arc at tStart, then the branch tip
            const int rootIdx = static_cast<int>(br.tStart * static_cast<float>(k_bezierSegs) + 0.5f);
            const glm::vec3 root = mainPts[std::min(rootIdx, k_bezierSegs)];

            // Tangent at tStart -- the branch diverges from the main bolt direction
            const glm::vec3 tang = evalBezierTangent(beam.origin, cp1, cp2, beam.hitPos, br.tStart);
            const float tangL = glm::length(tang);
            const glm::vec3 tangN = (tangL > 0.001f) ? tang / tangL : axisN;

            // Rotate tangent by br.angle in the perp plane
            const glm::vec3 brDir = glm::normalize(tangN * std::cos(br.angle) + perp * std::sin(br.angle));

            const float brLen = len * br.length;
            const glm::vec3 tip = root + brDir * brLen;
            const glm::vec3 brAxis = tip - root;

            // Sample branch path with 2-octave fBm (less detail than main bolt)
            std::vector<glm::vec3> brPts;
            brPts.reserve(k_branchSegs + 1);
            for (int j = 0; j <= k_branchSegs; ++j) {
                const float bt = static_cast<float>(j) / static_cast<float>(k_branchSegs);
                glm::vec3 bpt = root + brAxis * bt;

                // Same pinned envelope so branch also stays attached at the root
                const float benv = std::sin(bt * glm::pi<float>());
                const float du = fbm(bt, br.seed, beam.time * 1.8f, 2);
                const float dv = fbm(bt, br.seed + 7.3f, beam.time * 1.8f, 2);
                bpt += perp * (du * brLen * 0.22f * benv) + perp2 * (dv * brLen * 0.12f * benv);
                brPts.push_back(bpt);
            }

            // Branch glow layer
            appendArcStrip(brPts, 0.90f, {0.35f, 0.72f, 1.00f, 0.28f * fade}, camForward);

            // Branch core
            appendArcStrip(brPts, 0.22f, {0.88f, 0.96f, 1.00f, 0.80f * fade}, camForward);
        }
    }
}

// activeBeamCount

uint32_t HitscanEffect::activeBeamCount() const
{
    uint32_t c = 0;
    for (const auto& b : beams_)
        c += b.active ? 1u : 0u;
    return c;
}
