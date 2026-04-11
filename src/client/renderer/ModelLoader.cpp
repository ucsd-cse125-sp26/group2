/// @file ModelLoader.cpp
/// @brief Assimp model loading, embedded texture decoding, and scene-graph traversal.

#include "ModelLoader.hpp"

#include <SDL3/SDL_log.h>

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

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#include <assimp/GltfMaterial.h>
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

// Texture helpers

/// @brief Decode an Assimp embedded texture into RGBA pixel data.
/// @param embTex Assimp embedded texture pointer.
/// @param textures Output vector to append the decoded texture to.
/// @return True on success, false if the texture is null or decoding fails.
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

/// @brief Resolve an embedded texture by Assimp type and return its index.
/// @param mat Assimp material.
/// @param type Texture type to resolve.
/// @param scene Assimp scene.
/// @param textures Decoded texture storage.
/// @param embTexToDataIdx Mapping from Assimp texture index to textures[] index.
/// @return Index into textures[], or -1 if not found.
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

/// @brief Resolve the diffuse/base-colour texture, trying multiple Assimp types.
/// @return Index into textures[], or -1 if not found.
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

/// @brief Try multiple Assimp texture types for the glTF metallic-roughness map.
///
/// glTF stores metallic-roughness combined (B=metallic, G=roughness).
/// Assimp maps it to different types depending on version and importer path.
/// @return Index into textures[], or -1 if not found.
int resolveMetallicRoughnessTex(const aiMaterial* mat,
                                const aiScene* scene,
                                std::vector<TextureData>& textures,
                                std::vector<int>& embTexToDataIdx)
{
    // Try every known Assimp slot for PBR metallic-roughness (ordered by likelihood).
    for (aiTextureType type : {aiTextureType_UNKNOWN,           // type 18 — glTF importer default
                               aiTextureType_METALNESS,         // type 15 — explicit metallic
                               aiTextureType_DIFFUSE_ROUGHNESS, // type 16 — explicit roughness
                               aiTextureType_SPECULAR}) {       // type 2  — some exporters put ORM here
        int idx = resolveTexture(mat, type, scene, textures, embTexToDataIdx);
        if (idx >= 0)
            return idx;
    }
    return -1;
}

/// @brief Extract PBR material scalars from an Assimp material.
/// @param mat Assimp material.
/// @return Populated MaterialData.
MaterialData extractMaterial(const aiMaterial* mat)
{
    MaterialData out;

    aiColor4D baseColor;
    if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS)
        out.baseColorFactor = glm::vec4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
    else {
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

    // glTF alphaMode: "OPAQUE" (default), "MASK", or "BLEND".
    aiString alphaMode;
    if (mat->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
        if (std::string(alphaMode.C_Str()) == "MASK")
            out.alphaMode = AlphaMode::Mask;
        else if (std::string(alphaMode.C_Str()) == "BLEND")
            out.alphaMode = AlphaMode::Blend;
    }
    // Also detect transparency from opacity < 1 (non-glTF models).
    float opacity = 1.0f;
    if (mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS && opacity < 0.99f)
        out.alphaMode = AlphaMode::Blend;

    float cutoff = 0.5f;
    if (mat->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS)
        out.alphaCutoff = cutoff;

    return out;
}

// glm / Assimp matrix conversion

/// @brief Convert an Assimp row-major matrix to a GLM column-major matrix.
/// @param m Assimp 4x4 matrix.
/// @return Equivalent glm::mat4.
glm::mat4 aiToGlm(const aiMatrix4x4& m)
{
    return glm::transpose(glm::make_mat4(&m.a1));
}

// Recursive scene-graph traversal
//
// Key rules:
//   - Positions and normals are transformed by each node's accumulated world
//     matrix, with normals using the correct normal matrix (inverse-transpose
//     of the upper-left 3x3 of the world matrix).
//   - When a node's transform has a NEGATIVE determinant (mirror / reflection),
//     all face windings for its meshes are reversed so the geometry remains
//     front-facing after the reflection.
//   - UV coordinates are NOT transformed -- they are 2D and independent of the
//     3D node hierarchy.

/// @brief Recursively process an Assimp scene node, baking transforms into vertex data.
/// @param node Current Assimp node.
/// @param scene Assimp scene.
/// @param parentTransform Accumulated parent world transform.
/// @param outModel Output model receiving meshes and textures.
/// @param embTexToDataIdx Mapping from Assimp texture index to textures[] index.
void processNode(const aiNode* node,
                 const aiScene* scene,
                 const glm::mat4& parentTransform,
                 LoadedModel& outModel,
                 std::vector<int>& embTexToDataIdx)
{
    const glm::mat4 worldTransform = parentTransform * aiToGlm(node->mTransformation);

    // Normal matrix: inverse-transpose of the upper-left 3×3.
    const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

    // Negative determinant → the transform includes a reflection.
    // Reflections reverse face winding, so we must reverse index order
    // to keep front-faces correct for our CW-is-front-face convention.
    const bool negativeScale = (glm::determinant(glm::mat3(worldTransform)) < 0.0f);

    for (unsigned int ni = 0; ni < node->mNumMeshes; ++ni) {
        const unsigned int meshIdx = node->mMeshes[ni];
        const aiMesh* mesh = scene->mMeshes[meshIdx];
        if (!mesh->HasPositions() || mesh->mNumFaces == 0)
            continue;

        MeshData data;
        data.vertices.reserve(mesh->mNumVertices);

        const bool hasTangents = mesh->HasTangentsAndBitangents();

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            ModelVertex vert{};

            // Transform position to world space.
            const glm::vec4 localPos(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z, 1.0f);
            vert.position = glm::vec3(worldTransform * localPos);

            // Transform normal via the normal matrix (handles non-uniform scale
            // and reflections correctly).
            if (mesh->HasNormals()) {
                const glm::vec3 localN(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z);
                vert.normal = glm::normalize(normalMat * localN);
            }

            if (mesh->mTextureCoords[0] != nullptr)
                vert.texCoord = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};

            if (hasTangents) {
                const glm::vec3 localT(mesh->mTangents[v].x, mesh->mTangents[v].y, mesh->mTangents[v].z);
                const glm::vec3 localB(mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z);
                glm::vec3 T = glm::normalize(normalMat * localT);
                // Handedness: sign of triple product (N×T)·B
                const float w =
                    (glm::dot(glm::cross(vert.normal, T), glm::normalize(normalMat * localB)) < 0.0f) ? -1.0f : 1.0f;
                vert.tangent = glm::vec4(T, w);
            } else {
                vert.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }

            data.vertices.push_back(vert);
        }

        // Collect indices.  If the node has a reflection transform, reverse
        // each triangle's winding so faces remain front-facing.
        data.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3)
                continue; // skip degenerate faces

            if (negativeScale) {
                // Reverse winding: swap vertex 1 and 2.
                data.indices.push_back(face.mIndices[0]);
                data.indices.push_back(face.mIndices[2]);
                data.indices.push_back(face.mIndices[1]);
            } else {
                data.indices.push_back(face.mIndices[0]);
                data.indices.push_back(face.mIndices[1]);
                data.indices.push_back(face.mIndices[2]);
            }
        }

        // Resolve textures and material.
        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

            data.diffuseTexIndex = resolveDiffuseTex(mat, scene, outModel.textures, embTexToDataIdx);

            data.normalTexIndex = resolveTexture(mat, aiTextureType_NORMALS, scene, outModel.textures, embTexToDataIdx);
            if (data.normalTexIndex >= 0)
                outModel.textures[static_cast<size_t>(data.normalTexIndex)].isSRGB = false;

            data.metallicRoughnessTexIndex =
                resolveMetallicRoughnessTex(mat, scene, outModel.textures, embTexToDataIdx);
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
        processNode(node->mChildren[c], scene, worldTransform, outModel, embTexToDataIdx);
}

} // namespace

bool loadModel(const std::string& path, LoadedModel& outModel, bool flipUVs)
{
    Assimp::Importer importer;

    unsigned int flags = static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                   aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices);

    // UV coordinate convention
    // Vulkan (and DirectX) expect V=0 at the TOP of the image.
    // OpenGL, Blender, and many Sketchfab exports use V=0 at the BOTTOM.
    //
    // If a model's textures appear vertically flipped (upside-down text on
    // license plates, inverted logos, seeing the "inside" of textured parts),
    // set flipUVs=true when loading that model.  This applies Assimp's
    // aiProcess_FlipUVs which transforms V → (1 − V) at import time.
    //
    // Models authored for glTF (which specifies V=0 at top) should NOT need
    // this flag.  Models converted from OBJ/FBX/Blender formats often DO.
    if (flipUVs)
        flags |= aiProcess_FlipUVs;

    const aiScene* scene = importer.ReadFile(path, flags);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        SDL_Log("ModelLoader: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    std::vector<int> embTexToDataIdx(scene->mNumTextures, -1);

    processNode(scene->mRootNode, scene, glm::mat4(1.0f), outModel, embTexToDataIdx);

    // Log material summary for all meshes (debugging aid).
    for (size_t m = 0; m < outModel.meshes.size(); ++m) {
        const auto& md = outModel.meshes[m];
        SDL_Log("  mesh[%zu] albedo=%d mr=%d metallic=%.2f roughness=%.2f",
                m,
                md.diffuseTexIndex,
                md.metallicRoughnessTexIndex,
                static_cast<double>(md.material.metallicFactor),
                static_cast<double>(md.material.roughnessFactor));
    }

    SDL_Log("ModelLoader: loaded '%s' — %u mesh(es), %u texture(s)",
            path.c_str(),
            static_cast<unsigned>(outModel.meshes.size()),
            static_cast<unsigned>(outModel.textures.size()));

    return !outModel.meshes.empty();
}
