#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

/// @brief One vertex in a loaded 3-D model.
/// Layout (32 bytes, no padding):
///   offset  0 — position  (vec3, 12 B)
///   offset 12 — normal    (vec3, 12 B)
///   offset 24 — texCoord  (vec2,  8 B)
struct ModelVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

static_assert(sizeof(ModelVertex) == 32, "ModelVertex size mismatch — check padding");
static_assert(offsetof(ModelVertex, normal) == 12, "ModelVertex normal offset mismatch");
static_assert(offsetof(ModelVertex, texCoord) == 24, "ModelVertex texCoord offset mismatch");

/// @brief CPU-side mesh data ready for GPU upload.
struct MeshData
{
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
};

/// @brief Load a model file via Assimp and populate @p outMeshes.
///
/// All meshes are triangulated, normals are (re-)generated if missing, and
/// duplicate vertices are merged.  UV coordinates are V-flipped for SDL3 GPU
/// conventions.
///
/// @param path      Absolute path to the model file.
/// @param outMeshes Receives one MeshData per mesh found in the file.
/// @return True on success; false on failure (error logged via SDL_Log).
bool loadModel(const std::string& path, std::vector<MeshData>& outMeshes);
