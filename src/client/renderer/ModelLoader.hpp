#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

/// @brief One vertex in a loaded 3-D model (PBR-ready).
/// Layout (48 bytes, no padding):
///   offset  0 — position  (vec3, 12 B)
///   offset 12 — normal    (vec3, 12 B)
///   offset 24 — texCoord  (vec2,  8 B)
///   offset 32 — tangent   (vec4, 16 B)  w = bitangent sign (±1)
struct ModelVertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec4 tangent; ///< xyz = tangent direction, w = bitangent handedness (±1).
};

static_assert(sizeof(ModelVertex) == 48, "ModelVertex size mismatch — check padding");
static_assert(offsetof(ModelVertex, normal) == 12, "ModelVertex normal offset mismatch");
static_assert(offsetof(ModelVertex, texCoord) == 24, "ModelVertex texCoord offset mismatch");
static_assert(offsetof(ModelVertex, tangent) == 32, "ModelVertex tangent offset mismatch");

/// @brief PBR material scalar parameters extracted from Assimp/glTF.
/// glTF alpha mode.
enum class AlphaMode
{
    Opaque,
    Mask,
    Blend
};

struct MaterialData
{
    glm::vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 0.0f;  ///< Default dielectric (non-metal).
    float roughnessFactor = 0.5f; ///< Default mid-roughness.
    float aoStrength = 1.0f;
    float normalScale = 1.0f;
    glm::vec4 emissiveFactor{0.0f, 0.0f, 0.0f, 0.0f}; ///< rgb in xyz, w unused.
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;                         ///< Threshold for AlphaMode::Mask.
};

/// @brief CPU-side mesh data ready for GPU upload.
struct MeshData
{
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    int diffuseTexIndex = -1;           ///< Base colour / albedo texture.
    int normalTexIndex = -1;            ///< Normal map texture.
    int metallicRoughnessTexIndex = -1; ///< Combined metallic-roughness (glTF convention).
    int aoTexIndex = -1;                ///< Ambient occlusion texture.
    int emissiveTexIndex = -1;          ///< Emissive texture.
    MaterialData material;              ///< Scalar PBR factors.
};

/// @brief RGBA pixel data for one texture (decoded from embedded PNG/JPEG).
struct TextureData
{
    std::vector<uint8_t> pixels; ///< Row-major RGBA, 4 bytes per pixel.
    int width = 0;
    int height = 0;
    bool isSRGB = true; ///< True for color textures (albedo, emissive); false for data (normal, MR).
};

/// @brief Everything returned by loadModel().
struct LoadedModel
{
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
};

/// @brief Load a model file via Assimp and decode its embedded textures.
///
/// Extracts PBR material properties (metallic, roughness, emissive factors)
/// and tangent vectors for normal mapping.  Traverses the full scene-graph
/// and bakes per-node transforms into vertex data.
///
/// @param path        Absolute path to the model file (GLB, OBJ, FBX, …).
/// @param outModel    Filled on success.
/// @param flipUVs     Flip V texture coordinates (V = 1 − V).  Set true for
///                    models from tools that use V=0 at bottom (Blender, OBJ).
/// @return True on success; false on failure (error logged via SDL_Log).
bool loadModel(const std::string& path, LoadedModel& outModel, bool flipUVs = false);
