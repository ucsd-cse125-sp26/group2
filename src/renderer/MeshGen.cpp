#include "MeshGen.hpp"

#include <cmath>
#include <numbers>

namespace MeshGen
{

namespace
{

static constexpr float k_pi = static_cast<float>(std::numbers::pi);

// Helper: add one triangle (3 vertices) to the buffer
void tri(std::vector<Vertex>& out, Vertex a, Vertex b, Vertex c)
{
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
}

// Apply a brightness multiplier to a color (simple Gouraud-style shading)
glm::vec3 shade(glm::vec3 col, float s)
{
    return glm::clamp(col * s, glm::vec3(0.0f), glm::vec3(1.0f));
}

// Build a sphere mesh centered at `center` with given radius and color.
// Outputs triangle-list vertices.
void buildSphere(std::vector<Vertex>& out, glm::vec3 center, float radius, glm::vec3 color, int segs, int stacks)
{
    for (int lat = 0; lat < stacks; ++lat) {
        float theta0 = k_pi * static_cast<float>(lat) / static_cast<float>(stacks) - k_pi * 0.5f;
        float theta1 = k_pi * static_cast<float>(lat + 1) / static_cast<float>(stacks) - k_pi * 0.5f;

        for (int lon = 0; lon < segs; ++lon) {
            float phi0 = 2.0f * k_pi * static_cast<float>(lon) / static_cast<float>(segs);
            float phi1 = 2.0f * k_pi * static_cast<float>(lon + 1) / static_cast<float>(segs);

            auto vert = [&](float theta, float phi) -> Vertex {
                float x = std::cos(theta) * std::cos(phi);
                float y = std::sin(theta);
                float z = std::cos(theta) * std::sin(phi);
                float bright = 0.5f + 0.5f * y; // top brighter than bottom
                return {center + glm::vec3(x, y, z) * radius, shade(color, bright)};
            };

            Vertex v00 = vert(theta0, phi0);
            Vertex v10 = vert(theta1, phi0);
            Vertex v01 = vert(theta0, phi1);
            Vertex v11 = vert(theta1, phi1);

            tri(out, v00, v10, v11);
            tri(out, v00, v11, v01);
        }
    }
}

// Build a cylinder (open ends) from y=yBot to y=yTop with given radius.
void buildCylinder(std::vector<Vertex>& out, float radius, float yBot, float yTop, glm::vec3 color, int segs)
{
    for (int i = 0; i < segs; ++i) {
        float phi0 = 2.0f * k_pi * static_cast<float>(i) / static_cast<float>(segs);
        float phi1 = 2.0f * k_pi * static_cast<float>(i + 1) / static_cast<float>(segs);

        float x0 = std::cos(phi0) * radius;
        float z0 = std::sin(phi0) * radius;
        float x1 = std::cos(phi1) * radius;
        float z1 = std::sin(phi1) * radius;

        // Side brightness based on facing direction (simple directional light sim)
        float bright0 = 0.55f + 0.35f * std::abs(std::cos(phi0 - 0.5f));
        float bright1 = 0.55f + 0.35f * std::abs(std::cos(phi1 - 0.5f));

        Vertex bl = {{x0, yBot, z0}, shade(color, bright0)};
        Vertex br = {{x1, yBot, z1}, shade(color, bright1)};
        Vertex tl = {{x0, yTop, z0}, shade(color, bright0 * 1.1f)};
        Vertex tr = {{x1, yTop, z1}, shade(color, bright1 * 1.1f)};

        tri(out, bl, br, tr);
        tri(out, bl, tr, tl);
    }
}

// Build a hemisphere (top or bottom half of a sphere).
// If top==true, builds the upper hemisphere; otherwise lower.
void buildHemisphere(
    std::vector<Vertex>& out, glm::vec3 center, float radius, glm::vec3 color, int segs, int stacks, bool top)
{
    int start = top ? stacks / 2 : 0;
    int end = top ? stacks : stacks / 2;

    for (int lat = start; lat < end; ++lat) {
        float theta0 = k_pi * static_cast<float>(lat) / static_cast<float>(stacks) - k_pi * 0.5f;
        float theta1 = k_pi * static_cast<float>(lat + 1) / static_cast<float>(stacks) - k_pi * 0.5f;

        for (int lon = 0; lon < segs; ++lon) {
            float phi0 = 2.0f * k_pi * static_cast<float>(lon) / static_cast<float>(segs);
            float phi1 = 2.0f * k_pi * static_cast<float>(lon + 1) / static_cast<float>(segs);

            auto vert = [&](float theta, float phi) -> Vertex {
                float x = std::cos(theta) * std::cos(phi);
                float y = std::sin(theta);
                float z = std::cos(theta) * std::sin(phi);
                float bright = 0.5f + 0.5f * y;
                return {center + glm::vec3(x, y, z) * radius, shade(color, bright)};
            };

            Vertex v00 = vert(theta0, phi0);
            Vertex v10 = vert(theta1, phi0);
            Vertex v01 = vert(theta0, phi1);
            Vertex v11 = vert(theta1, phi1);

            tri(out, v00, v10, v11);
            tri(out, v00, v11, v01);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<Vertex> buildPlayerModel(
    glm::vec3 bodyColor, float capsuleRadius, float capsuleHalfHeight, float headRadius, int segments, int stacks)
{
    std::vector<Vertex> verts;
    verts.reserve(segments * stacks * 6 * 3); // rough upper bound

    // The capsule body:
    //   Lower hemisphere: center = (0, capsuleRadius, 0), radius = capsuleRadius
    //   Cylinder: from y=capsuleRadius to y=capsuleRadius + 2*capsuleHalfHeight
    //   Upper hemisphere: center = (0, capsuleRadius + 2*capsuleHalfHeight, 0), radius = capsuleRadius

    float bodyBot = capsuleRadius;
    float bodyTop = capsuleRadius + 2.0f * capsuleHalfHeight;

    // Lower hemisphere
    buildHemisphere(verts, {0.0f, bodyBot, 0.0f}, capsuleRadius, bodyColor, segments, stacks, false);

    // Cylinder body
    buildCylinder(verts, capsuleRadius, bodyBot, bodyTop, bodyColor, segments);

    // Upper hemisphere
    buildHemisphere(verts, {0.0f, bodyTop, 0.0f}, capsuleRadius, bodyColor, segments, stacks, true);

    // Head — slightly lighter color to distinguish from body
    glm::vec3 headColor = glm::clamp(bodyColor + glm::vec3(0.15f), glm::vec3(0.0f), glm::vec3(1.0f));
    float headCenter = bodyTop + capsuleRadius + headRadius * 0.8f; // sits on top of body
    buildSphere(verts, {0.0f, headCenter, 0.0f}, headRadius, headColor, segments, stacks);

    return verts;
}

} // namespace MeshGen
