/// @file SkinVertex.hpp
/// @brief Bind-pose vertex layout used by the CPU skinning pipeline.

#pragma once

#include <cstddef>
#include <glm/glm.hpp>

/// @brief One vertex in a skinned bind-pose mesh.
///
/// Layout (48 bytes, no padding):
///   offset  0 — position (vec3, 12 B)
///   offset 12 — normal   (vec3, 12 B)
///   offset 24 — texCoord (vec2,  8 B)
///   offset 32 — tangent  (vec4, 16 B) — w = bitangent sign (±1)
struct ModelVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent;
};

static_assert(sizeof(ModelVertex) == 48, "ModelVertex size mismatch — check padding");
static_assert(offsetof(ModelVertex, normal) == 12, "ModelVertex normal offset mismatch");
static_assert(offsetof(ModelVertex, texCoord) == 24, "ModelVertex texCoord offset mismatch");
static_assert(offsetof(ModelVertex, tangent) == 32, "ModelVertex tangent offset mismatch");
