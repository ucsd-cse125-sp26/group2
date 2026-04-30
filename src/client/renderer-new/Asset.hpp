//
// Created by mysteriousjim on 4/16/2026.
//

#ifndef GROUP2_MODEL_H
#define GROUP2_MODEL_H

#define TEX_CHANNELS 1
#include "glm/glm.hpp"

#include <unordered_map>
#include <vector>

using MeshIdInt = uint32_t;
using ModelIdInt = uint32_t;
using TexIdInt = uint32_t;
using MaterialIdInt = uint32_t;

namespace Asset
{

struct GeoBufferInfo
{
    void* srcData;
    SDL_GPUBuffer* gpuBuff;
    Uint32 bufferSize;
};

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texUV;
};

struct Mesh
{
    std::vector<Vertex> vertexData_;
    std::vector<uint32_t> indexData_;
    GeoBufferInfo vBufferInfo_;
    GeoBufferInfo iBufferInfo_;
};

struct Material
{
    glm::vec3 kDiffuse_;
    glm::vec3 kAmbient_;
    glm::vec3 kSpecular_;
    glm::vec3 kEmission_;
    float nSpecular;
    float nIor;
    TexIdInt texId_[TEX_CHANNELS];
};
struct ModelElement
{
    MeshIdInt meshId_;
    MaterialIdInt materialId_;
    glm::mat4 cachedTransform_;
};

struct ModelNode
{
    std::vector<uint32_t> childIndices_;
    std::vector<uint32_t> modelElementsIndices_;
    glm::mat4 transform_;
};

struct Model
{
    std::vector<ModelNode> modelNodes_;
    std::vector<ModelElement> modelElements_;
};

inline std::unordered_map<MeshIdInt, Mesh> meshes_;
inline std::unordered_map<ModelIdInt, Model> models_;
inline std::unordered_map<TexIdInt, uint32_t> textures_;
inline std::unordered_map<MaterialIdInt, Material> materials_;

inline uint32_t fnv1a32(const std::string& str) {
    uint32_t hash = 2166136261u;
    for (const char c : str)
        hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    return hash;
}

inline uint32_t getIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}

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

inline MaterialIdInt getMaterialIdFromString(const std::string& strId)
{
    return fnv1a32(strId);
}


} // namespace Asset
#endif // GROUP2_MODEL_H
