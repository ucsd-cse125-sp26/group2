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

struct Model
{
    MeshIdInt meshId_;
    TexIdInt texId_[TEX_CHANNELS];
};

inline std::unordered_map<MeshIdInt, Mesh> meshes_;
inline std::unordered_map<MeshIdInt, Model> models_;
inline std::unordered_map<MeshIdInt, uint32_t> textures_;

} // namespace Asset
#endif // GROUP2_MODEL_H
