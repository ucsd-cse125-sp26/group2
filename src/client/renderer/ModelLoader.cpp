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

#include <glm/gtc/type_ptr.hpp>

namespace
{

// ── Texture helpers ─────────────────────────────────────────────────────────

bool decodeEmbeddedTexture(const aiTexture* embTex, std::vector<TextureData>& textures)
{
    if (!embTex)
        return false;

    TextureData td;

    if (embTex->mHeight == 0) {
        int ch = 0;
        stbi_uc* raw = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(embTex->pcData),
                                             static_cast<int>(embTex->mWidth),
                                             &td.width,
                                             &td.height,
                                             &ch,
                                             4);
        if (!raw) {
            SDL_Log("ModelLoader: stb_image failed: %s", stbi_failure_reason());
            return false;
        }
        td.pixels.assign(raw, raw + static_cast<size_t>(td.width) * static_cast<size_t>(td.height) * 4);
        stbi_image_free(raw);
    } else {
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

    int embIdx = -1;
    for (unsigned int i = 0; i < scene->mNumTextures; ++i) {
        if (scene->mTextures[i] == embTex) {
            embIdx = static_cast<int>(i);
            break;
        }
    }

    if (embIdx >= 0 && embIdx < static_cast<int>(embTexToDataIdx.size()) &&
        embTexToDataIdx[static_cast<size_t>(embIdx)] >= 0)
    {
        return embTexToDataIdx[static_cast<size_t>(embIdx)];
    }

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
    else
        out.metallicFactor = 0.0f;

    float roughness = 0.5f;
    if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
        out.roughnessFactor = roughness;
    else
        out.roughnessFactor = 0.5f;

    aiColor3D emissive;
    if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
        out.emissiveFactor = glm::vec4(emissive.r, emissive.g, emissive.b, 0.0f);

    return out;
}

// ── Assimp mat4 → glm::mat4 ────────────────────────────────────────────────

glm::mat4 aiToGlm(const aiMatrix4x4& m)
{
    // Assimp is row-major, GLM is column-major — transpose on copy.
    return glm::transpose(glm::make_mat4(&m.a1));
}

// ── Recursive scene graph traversal ─────────────────────────────────────────

void processNode(const aiNode* node,
                 const aiScene* scene,
                 const glm::mat4& parentTransform,
                 LoadedModel& outModel,
                 std::vector<int>& embTexToDataIdx)
{
    const glm::mat4 globalTransform = parentTransform * aiToGlm(node->mTransformation);

    // Compute the normal matrix (inverse-transpose of upper-left 3×3).
    const glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(globalTransform)));

    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const unsigned int meshIdx = node->mMeshes[i];
        const aiMesh* mesh = scene->mMeshes[meshIdx];
        if (!mesh->HasPositions() || mesh->mNumFaces == 0)
            continue;

        MeshData data;
        data.vertices.reserve(mesh->mNumVertices);

        const bool hasTangents = mesh->HasTangentsAndBitangents();

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            ModelVertex vert{};

            // Bake the node hierarchy transform into vertex positions and normals.
            glm::vec4 localPos = glm::vec4(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z, 1.0f);
            vert.position = glm::vec3(globalTransform * localPos);

            if (mesh->HasNormals()) {
                glm::vec3 localNormal = {mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z};
                vert.normal = glm::normalize(normalMat * localNormal);
            }

            if (mesh->mTextureCoords[0] != nullptr)
                vert.texCoord = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};

            if (hasTangents) {
                glm::vec3 localT = {mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z};
                glm::vec3 localB = {mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z};
                glm::vec3 T = glm::normalize(normalMat * localT);
                float w =
                    (glm::dot(glm::cross(vert.normal, T), glm::normalize(normalMat * localB)) < 0.0f) ? -1.0f : 1.0f;
                vert.tangent = glm::vec4(T, w);
            } else {
                vert.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }

            data.vertices.push_back(vert);
        }

        data.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
                data.indices.push_back(face.mIndices[j]);
        }

        // Resolve textures and material.
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

            data.diffuseTexIndex = resolveDiffuseTex(mat, scene, outModel.textures, embTexToDataIdx);

            data.normalTexIndex = resolveTexture(mat, aiTextureType_NORMALS, scene, outModel.textures, embTexToDataIdx);
            if (data.normalTexIndex >= 0)
                outModel.textures[static_cast<size_t>(data.normalTexIndex)].isSRGB = false;

            data.metallicRoughnessTexIndex =
                resolveTexture(mat, aiTextureType_UNKNOWN, scene, outModel.textures, embTexToDataIdx);
            if (data.metallicRoughnessTexIndex < 0)
                data.metallicRoughnessTexIndex =
                    resolveTexture(mat, aiTextureType_METALNESS, scene, outModel.textures, embTexToDataIdx);
            if (data.metallicRoughnessTexIndex >= 0)
                outModel.textures[static_cast<size_t>(data.metallicRoughnessTexIndex)].isSRGB = false;

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

    // Recurse into children.
    for (unsigned int c = 0; c < node->mNumChildren; ++c)
        processNode(node->mChildren[c], scene, globalTransform, outModel, embTexToDataIdx);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════

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

    // Traverse the full node hierarchy, baking transforms into vertex positions.
    processNode(scene->mRootNode, scene, glm::mat4(1.0f), outModel, embTexToDataIdx);

    // Diagnostic: sample a few pixels from each albedo texture.
    for (size_t m = 0; m < outModel.meshes.size(); ++m) {
        const auto& md = outModel.meshes[m];
        if (md.diffuseTexIndex >= 0 && static_cast<size_t>(md.diffuseTexIndex) < outModel.textures.size()) {
            const auto& tex = outModel.textures[static_cast<size_t>(md.diffuseTexIndex)];
            if (tex.width > 0 && tex.height > 0) {
                // Sample center pixel.
                int cx = tex.width / 2;
                int cy = tex.height / 2;
                size_t idx = (static_cast<size_t>(cy) * static_cast<size_t>(tex.width) + static_cast<size_t>(cx)) * 4;
                if (idx + 3 < tex.pixels.size()) {
                    SDL_Log("  mesh[%zu] albedoTex[%d] %dx%d center=(%u,%u,%u,%u) sRGB=%d",
                            m,
                            md.diffuseTexIndex,
                            tex.width,
                            tex.height,
                            tex.pixels[idx],
                            tex.pixels[idx + 1],
                            tex.pixels[idx + 2],
                            tex.pixels[idx + 3],
                            tex.isSRGB ? 1 : 0);
                }
            }
        }
    }

    SDL_Log("ModelLoader: loaded '%s' — %u mesh(es), %u texture(s)",
            path.c_str(),
            static_cast<unsigned>(outModel.meshes.size()),
            static_cast<unsigned>(outModel.textures.size()));

    return !outModel.meshes.empty();
}
