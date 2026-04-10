#include "ModelLoader.hpp"

#include <SDL3/SDL_log.h>

// ---------------------------------------------------------------------------
// stb_image — single-header PNG/JPEG/etc. decoder.
// Define the implementation in exactly this one translation unit.
// ---------------------------------------------------------------------------
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// ---------------------------------------------------------------------------
// Assimp headers — suppress clang/gcc warnings from third-party code.
// ---------------------------------------------------------------------------
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

namespace
{

/// @brief Try to decode an Assimp embedded texture into RGBA pixels.
bool decodeEmbeddedTexture(const aiTexture* embTex, std::vector<TextureData>& textures)
{
    if (!embTex)
        return false;

    TextureData td;

    if (embTex->mHeight == 0) {
        // Compressed format (PNG / JPEG / …) stored as a raw byte blob.
        int ch = 0;
        stbi_uc* raw = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(embTex->pcData),
                                             static_cast<int>(embTex->mWidth),
                                             &td.width,
                                             &td.height,
                                             &ch,
                                             4);
        if (!raw) {
            SDL_Log("ModelLoader: stb_image failed to decode embedded texture: %s", stbi_failure_reason());
            return false;
        }

        td.pixels.assign(raw, raw + static_cast<size_t>(td.width) * static_cast<size_t>(td.height) * 4);
        stbi_image_free(raw);
    } else {
        // Raw BGRA8888 data — convert to RGBA.
        td.width = static_cast<int>(embTex->mWidth);
        td.height = static_cast<int>(embTex->mHeight);
        const size_t numPixels = static_cast<size_t>(td.width) * static_cast<size_t>(td.height);
        td.pixels.resize(numPixels * 4);

        for (size_t i = 0; i < numPixels; ++i) {
            td.pixels[i * 4 + 0] = embTex->pcData[i].r;
            td.pixels[i * 4 + 1] = embTex->pcData[i].g;
            td.pixels[i * 4 + 2] = embTex->pcData[i].b;
            td.pixels[i * 4 + 3] = embTex->pcData[i].a;
        }
    }

    textures.push_back(std::move(td));
    return true;
}

/// @brief Resolve an embedded texture of the given type from a material.
/// @return Index into textures vector, or -1 if not found.
int resolveTexture(const aiMaterial* mat,
                   aiTextureType type,
                   const aiScene* scene,
                   std::vector<TextureData>& textures,
                   std::vector<int>& embTexToDataIdx)
{
    if (mat->GetTextureCount(type) == 0)
        return -1;

    aiString texPath;
    if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS)
        return -1;

    const aiTexture* embTex = scene->GetEmbeddedTexture(texPath.C_Str());
    if (!embTex)
        return -1;

    // Compute the embedded-texture slot index for caching.
    int embIdx = -1;
    for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
        if (scene->mTextures[i] == embTex) {
            embIdx = static_cast<int>(i);
            break;
        }
    }

    // Return cached entry if already decoded.
    if (embIdx >= 0 && embIdx < static_cast<int>(embTexToDataIdx.size()) &&
        embTexToDataIdx[static_cast<size_t>(embIdx)] >= 0)
    {
        return embTexToDataIdx[static_cast<size_t>(embIdx)];
    }

    // Decode and store.
    const int dataIdx = static_cast<int>(textures.size());
    if (decodeEmbeddedTexture(embTex, textures)) {
        if (embIdx >= 0) {
            if (static_cast<size_t>(embIdx) >= embTexToDataIdx.size())
                embTexToDataIdx.resize(static_cast<size_t>(embIdx) + 1, -1);
            embTexToDataIdx[static_cast<size_t>(embIdx)] = dataIdx;
        }
        return dataIdx;
    }

    return -1;
}

/// @brief Resolve base-colour texture (tries BASE_COLOR first, then DIFFUSE).
int resolveDiffuseTex(const aiMaterial* mat,
                      const aiScene* scene,
                      std::vector<TextureData>& textures,
                      std::vector<int>& embTexToDataIdx)
{
    for (aiTextureType type : {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE}) {
        int idx = resolveTexture(mat, type, scene, textures, embTexToDataIdx);
        if (idx >= 0)
            return idx;
    }
    return -1;
}

/// @brief Extract scalar PBR material factors from an Assimp material.
MaterialData extractMaterial(const aiMaterial* mat)
{
    MaterialData out;

    aiColor4D baseColor;
    if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
        out.baseColorFactor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
    } else {
        aiColor4D diffuse;
        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
            out.baseColorFactor = glm::vec4(diffuse.r, diffuse.g, diffuse.b, diffuse.a);
    }

    float metallic = 0.0f;
    if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
        out.metallicFactor = metallic;

    float roughness = 0.5f;
    if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
        out.roughnessFactor = roughness;

    aiColor3D emissive;
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
        out.emissiveFactor = glm::vec4(emissive.r, emissive.g, emissive.b, 0.0f);

    return out;
}

} // namespace

bool loadModel(const std::string& path, LoadedModel& outModel)
{
    Assimp::Importer importer;

    const aiScene* scene =
        importer.ReadFile(path,
                          static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                    aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices));

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        SDL_Log("ModelLoader: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    std::vector<int> embTexToDataIdx(scene->mNumTextures, -1);
    outModel.meshes.reserve(scene->mNumMeshes);

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh->HasPositions() || mesh->mNumFaces == 0)
            continue;

        MeshData data;
        data.vertices.reserve(mesh->mNumVertices);

        const bool hasTangents = mesh->HasTangentsAndBitangents();

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            ModelVertex vert{};
            vert.position = {mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z};

            if (mesh->HasNormals())
                vert.normal = {mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z};

            if (mesh->mTextureCoords[0] != nullptr)
                vert.texCoord = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};

            if (hasTangents) {
                glm::vec3 T = {mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z};
                glm::vec3 B = {mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z};
                glm::vec3 N = vert.normal;
                // Compute handedness: sign of dot(cross(N, T), B).
                float w = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
                vert.tangent = glm::vec4(T, w);
            } else {
                vert.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // default tangent along +X
            }

            data.vertices.push_back(vert);
        }

        data.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int i = 0; i < face.mNumIndices; ++i)
                data.indices.push_back(face.mIndices[i]);
        }

        // Resolve textures and material for this mesh.
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

            data.diffuseTexIndex = resolveDiffuseTex(mat, scene, outModel.textures, embTexToDataIdx);

            data.normalTexIndex = resolveTexture(mat, aiTextureType_NORMALS, scene, outModel.textures, embTexToDataIdx);

            // glTF metallic-roughness is often stored under aiTextureType_UNKNOWN index 0.
            data.metallicRoughnessTexIndex =
                resolveTexture(mat, aiTextureType_UNKNOWN, scene, outModel.textures, embTexToDataIdx);
            if (data.metallicRoughnessTexIndex < 0)
                data.metallicRoughnessTexIndex =
                    resolveTexture(mat, aiTextureType_METALNESS, scene, outModel.textures, embTexToDataIdx);

            data.aoTexIndex =
                resolveTexture(mat, aiTextureType_AMBIENT_OCCLUSION, scene, outModel.textures, embTexToDataIdx);
            if (data.aoTexIndex < 0)
                data.aoTexIndex =
                    resolveTexture(mat, aiTextureType_LIGHTMAP, scene, outModel.textures, embTexToDataIdx);

            data.emissiveTexIndex =
                resolveTexture(mat, aiTextureType_EMISSIVE, scene, outModel.textures, embTexToDataIdx);

            data.material = extractMaterial(mat);
        }

        outModel.meshes.push_back(std::move(data));
    }

    SDL_Log("ModelLoader: loaded '%s' — %u mesh(es), %u texture(s)",
            path.c_str(),
            static_cast<unsigned>(outModel.meshes.size()),
            static_cast<unsigned>(outModel.textures.size()));

    return !outModel.meshes.empty();
}
