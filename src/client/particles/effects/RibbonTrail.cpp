#include "RibbonTrail.hpp"

#include "ecs/components/Position.hpp"
#include "ecs/components/RibbonEmitter.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

void RibbonTrail::update(float dt, Registry& registry, glm::vec3 camPos)
{
    vertices_.clear();

    registry.view<Position, RibbonEmitter>().each([&](const Position& pos, RibbonEmitter& ribbon) {
        // 1. Age all nodes; drop expired ones
        for (int i = 0; i < ribbon.count; ++i)
            ribbon.nodes[i].age += dt;

        // Remove nodes older than maxAge (scan, compact)
        int newCount = 0;
        for (int i = 0; i < ribbon.count; ++i) {
            if (ribbon.nodes[i].age < ribbon.maxAge)
                ribbon.nodes[newCount++] = ribbon.nodes[i];
        }
        ribbon.count = newCount;

        // 2. Record a new node if enough time has elapsed
        ribbon.recordAccumulator += dt;
        if (ribbon.recordAccumulator >= ribbon.recordInterval) {
            ribbon.recordAccumulator = 0.f;
            // Insert at head of ring buffer
            const int insertIdx = ribbon.head % RibbonEmitter::MaxNodes;
            ribbon.nodes[insertIdx].pos = pos.value;
            ribbon.nodes[insertIdx].age = 0.f;
            ribbon.head = (ribbon.head + 1) % RibbonEmitter::MaxNodes;
            ribbon.count = std::min(ribbon.count + 1, RibbonEmitter::MaxNodes);
        }

        // 3. Expand segments into RibbonVertex quads
        // Nodes are NOT in temporal order in the ring buffer; we need to
        // reconstruct the ordered list (newest = age 0 at tip).
        // Build a sorted (by age) local snapshot.
        struct SnapNode
        {
            glm::vec3 pos;
            float age;
        };
        std::vector<SnapNode> sorted;
        sorted.reserve(ribbon.count);
        for (int i = 0; i < ribbon.count; ++i)
            sorted.push_back({ribbon.nodes[i].pos, ribbon.nodes[i].age});
        std::sort(sorted.begin(), sorted.end(), [](const SnapNode& a, const SnapNode& b) { return a.age < b.age; });

        // Always prepend current position as tip (age=0)
        sorted.insert(sorted.begin(), {pos.value, 0.f});

        if (sorted.size() < 2)
            return;

        for (size_t i = 0; i + 1 < sorted.size(); ++i) {
            const SnapNode& nA = sorted[i];
            const SnapNode& nB = sorted[i + 1];

            const glm::vec3 axis = nB.pos - nA.pos;
            const float axLen = glm::length(axis);
            if (axLen < 0.001f)
                continue;

            const glm::vec3 axisN = axis / axLen;
            const glm::vec3 toEye = glm::normalize(camPos - (nA.pos + nB.pos) * 0.5f);
            const glm::vec3 side = glm::normalize(glm::cross(axisN, toEye)) * ribbon.width;

            const float tA = nA.age / ribbon.maxAge;
            const float tB = nB.age / ribbon.maxAge;

            auto lerpColor = [&](float t) -> glm::vec4 {
                glm::vec4 c = glm::mix(ribbon.tipColor, ribbon.tailColor, t);
                // Pre-multiply alpha
                c.r *= c.a;
                c.g *= c.a;
                c.b *= c.a;
                return c;
            };

            const glm::vec4 colA = lerpColor(tA);
            const glm::vec4 colB = lerpColor(tB);

            // Emit 6 vertices (TRIANGLELIST, 2 tris per segment)
            // Quad: (A-side, A+side, B+side, B-side)
            // Tri1: A-side, A+side, B+side
            // Tri2: A-side, B+side, B-side
            auto v = [&](glm::vec3 p, glm::vec4 c) { vertices_.push_back({p, 0.f, c}); };
            v(nA.pos - side, colA);
            v(nA.pos + side, colA);
            v(nB.pos + side, colB);

            v(nA.pos - side, colA);
            v(nB.pos + side, colB);
            v(nB.pos - side, colB);
        }
    });
}
