#include "HitscanEffect.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <glm/gtc/constants.hpp>

static float randf()
{
    return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
}
static float randSigned()
{
    return randf() * 2.f - 1.f;
}

// ---------------------------------------------------------------------------
// Midpoint displacement to generate jagged arc
// ---------------------------------------------------------------------------

void HitscanEffect::displaceSegment(std::vector<glm::vec3>& pts, int depth, float maxOffset, glm::vec3 camForward)
{
    if (depth == 0 || pts.size() < 2)
        return;

    std::vector<glm::vec3> result;
    result.reserve(pts.size() * 2 - 1);

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        result.push_back(pts[i]);
        const glm::vec3 mid = (pts[i] + pts[i + 1]) * 0.5f;
        const glm::vec3 seg = pts[i + 1] - pts[i];
        const float segLen = glm::length(seg);
        if (segLen > 0.001f) {
            const glm::vec3 perp = glm::normalize(glm::cross(seg, camForward));
            result.push_back(mid + perp * randSigned() * maxOffset * segLen);
        } else {
            result.push_back(mid);
        }
    }
    result.push_back(pts.back());

    pts = std::move(result);
    displaceSegment(pts, depth - 1, maxOffset * 0.55f, camForward);
}

// ---------------------------------------------------------------------------
// Expand polyline → triangle strip vertices
// ---------------------------------------------------------------------------

void HitscanEffect::expandArcToVerts(const std::vector<glm::vec3>& pts,
                                     float arcRadius,
                                     glm::vec4 color,
                                     std::vector<ArcVertex>& out)
{
    // Degenerate triangle to separate from previous strip
    if (!out.empty()) {
        out.push_back(out.back()); // duplicate last
        out.push_back({pts[0], 0.f, color});
    }

    for (size_t i = 0; i < pts.size(); ++i) {
        glm::vec3 dir{0.f, 1.f, 0.f};
        if (i + 1 < pts.size())
            dir = glm::normalize(pts[i + 1] - pts[i]);
        else if (i > 0)
            dir = glm::normalize(pts[i] - pts[i - 1]);

        const glm::vec3 perp = glm::normalize(glm::cross(dir, glm::vec3{0.f, 0.f, 1.f}));
        const glm::vec3 side = perp * arcRadius;

        out.push_back({pts[i] - side, 1.f, color});
        out.push_back({pts[i] + side, -1.f, color});
    }
}

// ---------------------------------------------------------------------------
// Generate main arc + 2–3 branch arcs
// ---------------------------------------------------------------------------

void HitscanEffect::generateArcs(
    glm::vec3 origin, glm::vec3 hitPos, glm::vec3 camForward, glm::vec4 color, float maxLifetime)
{
    LiveArc arc;
    arc.lifetime = maxLifetime;
    arc.maxLifetime = maxLifetime;

    // Main arc
    std::vector<glm::vec3> pts = {origin, hitPos};
    displaceSegment(pts, 4, 0.12f, camForward);
    expandArcToVerts(pts, k_arcRadius, color, arc.verts);

    // Branch arcs: 2–3 off random midpoints
    const int branches = 2 + (std::rand() % 2);
    for (int b = 0; b < branches; ++b) {
        if (pts.size() < 3)
            break;
        const size_t pivot = 1 + std::rand() % (pts.size() - 2);
        const glm::vec3 branchEnd = pts[pivot] +
                                    glm::normalize(hitPos - origin) * (glm::length(hitPos - origin) * 0.3f) +
                                    glm::vec3{randSigned(), randSigned(), randSigned()} * 80.f;

        std::vector<glm::vec3> bpts = {pts[pivot], branchEnd};
        displaceSegment(bpts, 2, 0.15f, camForward);
        expandArcToVerts(bpts, k_arcRadius * 0.6f, color * glm::vec4{0.7f, 0.7f, 0.7f, 0.7f}, arc.verts);
    }

    arcs_.push_back(std::move(arc));
}

// ---------------------------------------------------------------------------
// Spawn
// ---------------------------------------------------------------------------

void HitscanEffect::spawn(glm::vec3 origin, glm::vec3 hitPos, WeaponType wt, glm::vec3 camForward)
{
    (void)wt; // could vary color/radius by weapon type in the future

    auto* beam = beamPool_.spawn();
    if (beam) {
        beam->origin = origin;
        beam->radius = 1.5f;
        beam->hitPos = hitPos;
        beam->lifetime = k_beamLifetime;
        beam->coreColor = {0.5f, 0.9f, 1.0f, 1.0f};
        beam->edgeColor = {0.0f, 0.3f, 0.8f, 0.0f};
    }

    generateArcs(origin, hitPos, camForward, glm::vec4{0.3f, 0.7f, 1.0f, 0.9f}, k_beamLifetime);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void HitscanEffect::update(float dt)
{
    beamPool_.update([&](HitscanBeam& b) -> bool {
        b.lifetime -= dt;
        return b.lifetime > 0.f;
    });

    for (auto it = arcs_.begin(); it != arcs_.end();) {
        it->lifetime -= dt;
        if (it->lifetime <= 0.f)
            it = arcs_.erase(it);
        else
            ++it;
    }

    // Rebuild flat arc vertex array with alpha scaled by remaining lifetime
    arcVerts_.clear();
    for (auto& arc : arcs_) {
        const float alpha = arc.lifetime / arc.maxLifetime;
        for (ArcVertex v : arc.verts) {
            v.color.a *= alpha;
            arcVerts_.push_back(v);
        }
    }
}
