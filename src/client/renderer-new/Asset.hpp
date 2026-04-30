/// @file Asset.hpp
/// @brief GPU asset types and global registries for the new renderer.
///
/// Defines vertex, mesh, model, and scene-instance data structures used by
/// NewRenderer and AssetLoader.  Global hash-map registries (meshes_, models_,
/// textures_) store loaded assets keyed by FNV-1a string hashes.

#pragma once

#define TEX_CHANNELS 1
#include "glm/glm.hpp"

#include <unordered_map>
#include <vector>

using MeshIdInt = uint32_t;
using ModelIdInt = uint32_t;
using TexIdInt = uint32_t;

namespace Asset
{

/// @brief References a CPU-side source pointer and its corresponding GPU buffer.
struct GeoBufferInfo
{
    void* srcData;
    SDL_GPUBuffer* gpuBuff;
    Uint32 bufferSize;
};

/// @brief Per-vertex attributes: position, normal, and texture coordinates.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texUV;
};

/// @brief A single mesh: CPU-side vertex/index data plus GPU buffer info.
struct Mesh
{
    std::vector<Vertex> vertexData_;
    std::vector<uint32_t> indexData_;
    GeoBufferInfo vBufferInfo_;
    GeoBufferInfo iBufferInfo_;
};

/// @brief One element of a model: a mesh reference, its local transform, and texture bindings.
struct ModelElement
{
    MeshIdInt meshId_;
    glm::mat4 modelElementTransform_;
    TexIdInt texId_[TEX_CHANNELS];
};

/// @brief A model composed of one or more ModelElement entries.
struct Model
{
    std::vector<ModelElement> modelElements_;
};

/// @brief A placed instance of a model in the scene with a world-space transform.
struct ModelInstance
{
    MeshIdInt modelId_;
    glm::mat4 modelMat_;
};

/// @brief A flat list of model instances ready for rendering.
struct FlattenedScene
{
    std::vector<ModelInstance> models_;
};

inline std::unordered_map<MeshIdInt, Mesh> meshes_;
inline std::unordered_map<ModelIdInt, Model> models_;
inline std::unordered_map<TexIdInt, uint32_t> textures_;

/// @brief Compute a 32-bit FNV-1a hash of the given string.
/// @param str The input string to hash.
/// @return The 32-bit FNV-1a hash value.
inline uint32_t fnv1a32(const std::string& str)
{
    uint32_t hash = 2166136261u;
    for (const char c : str)
        hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    return hash;
}

/// @brief Convert a string identifier to a generic 32-bit hash ID.
/// @param strId The string identifier.
/// @return The hashed ID.
inline uint32_t getIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}

/// @brief Convert a string identifier to a MeshIdInt hash.
/// @param strId The string identifier for the mesh.
/// @return The hashed mesh ID.
inline MeshIdInt getMeshIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}

inline ModelIdInt getModelIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}

inline TexIdInt getTexIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}

} // namespace Asset
