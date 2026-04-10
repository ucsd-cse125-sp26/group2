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
    int diffuseTexIndex = -1; ///< Index into LoadedModel::textures, or -1 if none.
};

/// @brief RGBA pixel data for one texture (decoded from embedded PNG/JPEG).
struct TextureData
{
    std::vector<uint8_t> pixels; ///< Row-major RGBA, 4 bytes per pixel.
    int width = 0;
    int height = 0;
};

/// @brief Everything returned by loadModel().
struct LoadedModel
{
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
};

/// @brief Load a model file via Assimp and decode its embedded textures.
///
/// Each mesh in @p outModel.meshes has a @c diffuseTexIndex pointing into
/// @p outModel.textures, or -1 when no base-colour texture is available.
/// Textures are decoded to RGBA via stb_image.
///
/// @param path     Absolute path to the model file (GLB, OBJ, FBX, …).
/// @param outModel Filled on success.
/// @return True on success; false on failure (error logged via SDL_Log).
bool loadModel(const std::string& path, LoadedModel& outModel);
