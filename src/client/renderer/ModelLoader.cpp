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
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

namespace
{

/// @brief Try to decode an Assimp embedded texture into RGBA pixels.
/// @return True on success (pushes one TextureData into @p textures).
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
                                             4); // force RGBA output

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

/// @brief Resolve the base-colour texture for @p mat and return its index
///        inside @p textures (loading it if not already cached).
/// @return The TextureData index, or -1 if the material has no colour texture.
int resolveDiffuseTex(const aiMaterial* mat,
                      const aiScene* scene,
                      std::vector<TextureData>& textures,
                      std::vector<int>& embTexToDataIdx)
{
    // GLTF 2.0 PBR base colour lives at aiTextureType_BASE_COLOR (Assimp 5+).
    // Older/fallback path is aiTextureType_DIFFUSE.
    for (aiTextureType type : {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE}) {
        if (mat->GetTextureCount(type) == 0)
            continue;

        aiString texPath;
        if (mat->GetTexture(type, 0, &texPath) != AI_SUCCESS)
            continue;

        // Embedded textures use path "*N" where N is the scene texture index.
        const aiTexture* embTex = scene->GetEmbeddedTexture(texPath.C_Str());
        if (!embTex)
            continue;

        // Compute the embedded-texture slot index so we can cache/deduplicate.
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
        break;
    }

    return -1;
}

} // namespace

bool loadModel(const std::string& path, LoadedModel& outModel)
{
    Assimp::Importer importer;

    // aiProcess_Triangulate         — convert n-gons to triangles
    // aiProcess_GenSmoothNormals     — generate normals only when missing
    // aiProcess_JoinIdenticalVertices — de-duplicate vertices
    // NOTE: aiProcess_FlipUVs is intentionally omitted — GLTF 2.0 and Vulkan
    //       both use top-left (0,0) UV convention, so no flip is needed.
    const aiScene* scene =
        importer.ReadFile(path,
                          static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                    aiProcess_JoinIdenticalVertices));

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        SDL_Log("ModelLoader: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    // Cache: embeddedTextureIndex → TextureData array index (-1 = not loaded yet)
    std::vector<int> embTexToDataIdx(scene->mNumTextures, -1);

    outModel.meshes.reserve(scene->mNumMeshes);

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh->HasPositions() || mesh->mNumFaces == 0)
            continue;

        MeshData data;
        data.vertices.reserve(mesh->mNumVertices);

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            ModelVertex vert{};
            vert.position = {mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z};

            if (mesh->HasNormals())
                vert.normal = {mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z};

            if (mesh->mTextureCoords[0] != nullptr)
                vert.texCoord = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};

            data.vertices.push_back(vert);
        }

        data.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int i = 0; i < face.mNumIndices; ++i)
                data.indices.push_back(face.mIndices[i]);
        }

        // Resolve base-colour texture for this mesh's material.
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            data.diffuseTexIndex =
                resolveDiffuseTex(scene->mMaterials[mesh->mMaterialIndex], scene, outModel.textures, embTexToDataIdx);
        }

        outModel.meshes.push_back(std::move(data));
    }

    SDL_Log("ModelLoader: loaded '%s' — %u mesh(es), %u texture(s)",
            path.c_str(),
            static_cast<unsigned>(outModel.meshes.size()),
            static_cast<unsigned>(outModel.textures.size()));

    return !outModel.meshes.empty();
}
