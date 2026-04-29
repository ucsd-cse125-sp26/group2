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

struct ModelElement
{
    MeshIdInt meshId_;
    glm::mat4 modelElementTransform_;
    TexIdInt texId_[TEX_CHANNELS];
};

struct Model
{
    std::vector<ModelElement> modelElements_;
};

struct ModelInstance
{
    MeshIdInt modelId_;
    glm::mat4 modelMat_;
};

struct FlattenedScene
{
    std::vector<ModelInstance> models_;
};

inline std::unordered_map<MeshIdInt, Mesh> meshes_;
inline std::unordered_map<ModelIdInt, Model> models_;
inline std::unordered_map<TexIdInt, uint32_t> textures_;

inline uint32_t fnv1a32(const std::string& str)
{
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

} // namespace Asset
#endif // GROUP2_MODEL_H
