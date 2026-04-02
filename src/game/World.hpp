#pragma once
#include <glm/glm.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// Vertex layout matching scene.vert
// ---------------------------------------------------------------------------

struct Vertex
{
    glm::vec3 position;
    glm::vec3 color;
};

// ---------------------------------------------------------------------------
// Static AABB box — used for both collision and mesh generation.
// The mesh uses face-brightness shading to give depth cues without lighting.
// ---------------------------------------------------------------------------

struct WorldBox
{
    glm::vec3 center;
    glm::vec3 half;  // half-extents
    glm::vec3 color; // base RGB
};

// ---------------------------------------------------------------------------
// World
// ---------------------------------------------------------------------------

struct World
{
    std::vector<WorldBox> boxes;
    std::vector<Vertex> mesh; // pre-built, uploaded once to GPU

    void addBox(glm::vec3 center, glm::vec3 half, glm::vec3 color)
    {
        boxes.push_back({center, half, color});
        appendBoxMesh(center, half, color);
    }

    // Convenience: build from min/max corners
    void addBoxMinMax(glm::vec3 mn, glm::vec3 mx, glm::vec3 color)
    {
        addBox((mn + mx) * 0.5f, (mx - mn) * 0.5f, color);
    }

private:
    // Face brightness factors: top, bottom, front(-Z), back(+Z), left(-X), right(+X)
    static constexpr float k_shade[6] = {1.00f, 0.45f, 0.75f, 0.65f, 0.60f, 0.70f};

    void appendBoxMesh(glm::vec3 c, glm::vec3 h, glm::vec3 col)
    {
        const glm::vec3 p[8] = {
            {c.x - h.x, c.y - h.y, c.z - h.z}, // 0: ---
            {c.x + h.x, c.y - h.y, c.z - h.z}, // 1: +--
            {c.x + h.x, c.y + h.y, c.z - h.z}, // 2: ++-
            {c.x - h.x, c.y + h.y, c.z - h.z}, // 3: -+-
            {c.x - h.x, c.y - h.y, c.z + h.z}, // 4: --+
            {c.x + h.x, c.y - h.y, c.z + h.z}, // 5: +-+
            {c.x + h.x, c.y + h.y, c.z + h.z}, // 6: +++
            {c.x - h.x, c.y + h.y, c.z + h.z}, // 7: -++
        };

        // Each face: 4 indices (quad → 2 triangles, CCW winding)
        // Faces ordered: +Y top, -Y bot, -Z front, +Z back, -X left, +X right
        const int faces[6][4] = {
            {3, 7, 6, 2}, // +Y top
            {0, 1, 5, 4}, // -Y bottom
            {0, 3, 2, 1}, // -Z front
            {4, 5, 6, 7}, // +Z back
            {0, 4, 7, 3}, // -X left
            {1, 2, 6, 5}, // +X right
        };

        for (int f = 0; f < 6; f++) {
            glm::vec3 fc = glm::clamp(col * k_shade[f], 0.0f, 1.0f);
            const int* idx = faces[f];
            // Triangle 1
            mesh.push_back({p[idx[0]], fc});
            mesh.push_back({p[idx[1]], fc});
            mesh.push_back({p[idx[2]], fc});
            // Triangle 2
            mesh.push_back({p[idx[0]], fc});
            mesh.push_back({p[idx[2]], fc});
            mesh.push_back({p[idx[3]], fc});
        }
    }
};

// ---------------------------------------------------------------------------
// Level builder — "Gravity Dojo" test map
// All units are Quake units (1 qu ≈ 3.175 cm).
// Player is 64 qu wide, 72 qu tall, eye at centre+28.
// ---------------------------------------------------------------------------

namespace level
{

constexpr glm::vec3 GRAY = {0.52f, 0.52f, 0.52f};
constexpr glm::vec3 DARK_GRAY = {0.32f, 0.32f, 0.32f};
constexpr glm::vec3 BLUE_GRAY = {0.38f, 0.40f, 0.55f};
constexpr glm::vec3 TEAL = {0.28f, 0.58f, 0.58f};
constexpr glm::vec3 BROWN = {0.60f, 0.42f, 0.24f};
constexpr glm::vec3 ORANGE = {0.80f, 0.48f, 0.16f};
constexpr glm::vec3 RUST = {0.65f, 0.28f, 0.16f};

inline World build()
{
    World w;

    // --- Main floor -------------------------------------------------------
    w.addBox({0, -20, 0}, {2000, 20, 2000}, GRAY);

    // --- Bhop runway (run toward +Z, side walls keep you in the lane) -----
    // Left & right walls; open on top; player builds speed by strafing L/R
    w.addBox({-204, 100, 650}, {4, 100, 550}, BLUE_GRAY); // left wall
    w.addBox({204, 100, 650}, {4, 100, 550}, BLUE_GRAY);  // right wall
    // End ramp-bump to practice preserving speed on re-land
    w.addBox({0, 16, 1220}, {200, 16, 8}, BLUE_GRAY);

    // --- Walljump canyon (run toward -Z, alternate between close walls) ---
    w.addBox({-80, 200, -600}, {4, 200, 500}, TEAL);
    w.addBox({80, 200, -600}, {4, 200, 500}, TEAL);
    // Back wall to catch the player at the end
    w.addBox({0, 100, -1110}, {88, 100, 10}, TEAL);

    // --- Platform course (+X direction, test jump height & glide) ---------
    w.addBox({400, 64, 0}, {150, 64, 150}, BROWN);   // low  (top y=128)
    w.addBox({650, 128, 0}, {125, 128, 125}, BROWN); // mid  (top y=256)
    w.addBox({900, 96, 0}, {125, 96, 125}, BROWN);   // high (top y=192)
    // Stepping stone between platform 1 and 2
    w.addBox({545, 96, 0}, {20, 96, 80}, BROWN);

    // --- Tall tower for aerial manoeuvres (-X quadrant) ------------------
    w.addBox({-500, 300, 0}, {50, 300, 50}, ORANGE); // 600 qu tall
    w.addBox({-500, 300, 160}, {50, 300, 50}, RUST); // second tower

    // --- Cover / parkour obstacles (open arena area) ----------------------
    w.addBox({-300, 48, -200}, {64, 48, 64}, DARK_GRAY);
    w.addBox({-150, 96, 300}, {48, 96, 48}, DARK_GRAY);
    w.addBox({300, 64, -600}, {80, 64, 80}, DARK_GRAY);
    w.addBox({-600, 32, 500}, {96, 32, 96}, DARK_GRAY);

    // --- Perimeter walls (so player can't fall off) -----------------------
    w.addBox({0, 100, 2004}, {2000, 100, 4}, DARK_GRAY);  // north
    w.addBox({0, 100, -2004}, {2000, 100, 4}, DARK_GRAY); // south
    w.addBox({2004, 100, 0}, {4, 100, 2000}, DARK_GRAY);  // east
    w.addBox({-2004, 100, 0}, {4, 100, 2000}, DARK_GRAY); // west

    return w;
}

} // namespace level
