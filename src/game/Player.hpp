#pragma once

#include <glm/vec2.hpp>

// ---------------------------------------------------------------------------
// Player — tracks world-space position and movement speed.
//
// Convention: +X = right, +Y = up (each renderer maps to its own NDC space).
// ---------------------------------------------------------------------------

struct Player
{
    glm::vec2 pos{0.0f, 0.0f};
    float speed = 1.0f; // world units per second
};
