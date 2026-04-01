#pragma once
#include "../game/World.hpp"

#include <glm/glm.hpp>
#include <vector>

// ---------------------------------------------------------------------------
// Procedural player-model mesh generation
//
// Each player model consists of:
//   • A pill-shaped body  — cylinder + 2 hemispheres (capsule)
//   • A sphere head       — smaller sphere on top of the body
//
// All geometry is returned as a flat array of Vertex structs (position+color)
// using triangle lists, ready to be uploaded directly to a GPU vertex buffer.
// The model is in "local" space: feet at y = 0, standing axis = +Y.
// ---------------------------------------------------------------------------

namespace MeshGen
{

// Team tint colors: 0 = self (green), 1 = blue, 2 = orange, 3 = red
static constexpr glm::vec3 k_playerColors[4] = {
    {0.20f, 0.85f, 0.30f}, // self / slot 0
    {0.25f, 0.45f, 0.90f}, // slot 1
    {0.90f, 0.55f, 0.15f}, // slot 2
    {0.85f, 0.20f, 0.20f}, // slot 3
};

// Build a player model centered at origin (feet at y=0).
// capsuleRadius / capsuleHalfHeight and headRadius in Quake units.
std::vector<Vertex> buildPlayerModel(glm::vec3 bodyColor,
                                     float capsuleRadius     = 32.0f,
                                     float capsuleHalfHeight = 36.0f,
                                     float headRadius        = 18.0f,
                                     int segments            = 12,
                                     int stacks              = 6);

} // namespace MeshGen
