/// @file Asset.hpp
/// @brief GPU asset types and global registries for the new renderer.
///
/// Defines vertex, mesh, model, and scene-instance data structures used by
/// NewRenderer and AssetLoader.  Global hash-map registries (meshes_, models_,
/// textures_) store loaded assets keyed by FNV-1a string hashes.

#pragma once

#define MATERIAL_MAX_TEXTURE_COUNT 3
#define MODEL_ROOT_NODE_INDEX 0
#define ASSETS_DIR "assets"
#include "glm/glm.hpp"

#include <glm/gtc/quaternion.hpp>
#include <stb_image.h>
#include <string>
#include <unordered_map>
#include <vector>

using MeshIdInt = uint32_t;
using ModelIdInt = uint32_t;
using TexIdInt = uint32_t;
using MaterialIdInt = uint32_t;

namespace Asset
{

/// @brief References a CPU-side source pointer and its corresponding GPU buffer.
struct GeoBufferInfo
{
    void* srcData;
    SDL_GPUBuffer* gpuBuff;
    Uint32 bufferSize;
};

/// @brief Per-vertex attributes: position, normal, texture coordinates, and tangent.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texUV;
    glm::vec4 tangent;
    glm::vec2 lightMapUV;
};

struct AABB
{
    glm::vec4 min{FLT_MAX};
    glm::vec4 max{-FLT_MAX};
};
/// @brief A single mesh: CPU-side vertex/index data plus GPU buffer info.
struct Mesh
{
    std::vector<Vertex> vertexData_;
    std::vector<uint32_t> indexData_;
    GeoBufferInfo vBufferInfo_;
    GeoBufferInfo iBufferInfo_;
    AABB aabb_;
};



struct CpuMesh
{
    std::vector<Vertex> vertexData_;
    std::vector<uint32_t> indexData_;
};

struct GpuMesh
{
    GeoBufferInfo vBufferInfo_;
    GeoBufferInfo iBufferInfo_;
};

struct Material
{
    glm::vec3 kDiffuse_ = glm::vec3(0.8f);
    glm::vec3 kAmbient_ = glm::vec3(0.08f);
    glm::vec3 kSpecular_ = glm::vec3(0.0f);
    glm::vec3 kEmission_ = glm::vec3(0.0f);
    float nSpecular = 32.0f;
    float nIor = 1.0f;
    bool hasPhongData_ = false;
    TexIdInt diffuseTexture = 0;
    TexIdInt normalTexture = 0;
    TexIdInt metallicRoughnessTexture = 0;
};

struct Texture
{
    stbi_uc* tex_raw = nullptr;
    SDL_GPUTexture* tex = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
};

struct ModelElement
{
    MeshIdInt meshId_;
    MaterialIdInt materialId_ = 0;
    glm::mat4 cachedTransform_;
    AABB cachedAabb_;
;
};

struct ModelNode
{
    std::vector<uint32_t> childIndices_;
    std::vector<uint32_t> modelElementsIndices_;
    glm::mat4 transform_;
};

struct MountPoint
{
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct PointLight
{
    glm::vec3 position{0.0f};
    float intensity = 1.0f;
    glm::vec3 color{1.0f};
    float range = 12.5f;
};

struct Model
{
    std::vector<ModelNode> modelNodes_;
    std::vector<ModelElement> modelElements_;
    std::vector<PointLight> pointLights;
    std::unordered_map<std::string, MountPoint> mountPoints;
    bool hasMuzzle = false;
    glm::vec3 muzzleLocalPos{};
};

struct ModelInstance
{
    ModelIdInt modelId_;
    glm::mat4 transform_;
    bool drawInScenePass = true;
};

inline std::unordered_map<MeshIdInt, Mesh> meshes_;
inline std::unordered_map<ModelIdInt, Model> models_;
inline std::unordered_map<TexIdInt, Texture> textures_;
inline std::unordered_map<MaterialIdInt, Material> materials_;

inline std::vector<ModelInstance> modelInstances_;
inline ModelIdInt weaponModelId_ = 0;
inline glm::mat4 weaponViewModel_ = glm::mat4(1.0f);

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

/// @brief Convert a string identifier to a ModelIdInt hash.
/// @param strId The string identifier for the model.
/// @return The hashed model ID.
inline ModelIdInt getModelIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}

/// @brief Convert a string identifier to a TexIdInt hash.
/// @param strId The string identifier for the texture.
/// @return The hashed texture ID.
inline TexIdInt getTexIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}

inline MaterialIdInt getMaterialIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}

} // namespace Asset
