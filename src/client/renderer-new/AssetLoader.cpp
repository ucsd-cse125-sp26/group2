/// @file AssetLoader.cpp
/// @brief Implementation of AssetLoader — Assimp scene import and mesh extraction.

#include "AssetLoader.hpp"

#include "Asset.hpp"
#include "glm/ext/scalar_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <stack>
#include <stb_image.h>
#include <unordered_map>
#include <vector>

namespace
{
bool g_loggedMissingTangents = false;
constexpr float k_defaultPointLightRange = 12.5f;

bool getMaterialTexturePath(aiMaterial& material, aiTextureType type, aiString& outPath)
{
    return material.GetTexture(type, 0, &outPath) == AI_SUCCESS && outPath.length > 0;
}

TexIdInt
loadEmbeddedMaterialTexture(const aiScene& sceneAi, const std::string& assetIdNameSpace, const aiString& texPath)
{
    const TexIdInt texId = Asset::getTexIdFromString(assetIdNameSpace + "texture_" + std::string(texPath.C_Str()));
    Asset::Texture& texture = Asset::textures_[texId];

    if (texture.tex_raw != nullptr || texture.tex != nullptr)
        return texId;

    const aiTexture* embeddedTexture = sceneAi.GetEmbeddedTexture(texPath.C_Str());
    if (!embeddedTexture || embeddedTexture->mHeight != 0) {
        SDL_Log("AssetLoader: skipping unsupported material texture '%s' "
                "(expected compressed embedded texture, embedded=%s, height=%u)",
                texPath.C_Str(),
                embeddedTexture ? "true" : "false",
                embeddedTexture ? embeddedTexture->mHeight : 0);
        return 0;
    }

    stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(embeddedTexture->pcData),
                                            static_cast<int>(embeddedTexture->mWidth),
                                            &texture.width,
                                            &texture.height,
                                            &texture.channels,
                                            4);

    if (!pixels) {
        std::cout << "stbi failed for " << texPath.C_Str() << ": " << stbi_failure_reason() << std::endl;
        return 0;
    }

    texture.tex_raw = pixels;
    return texId;
}
} // namespace

const aiScene* AssetLoader::loadAsset(Assimp::Importer& importer, const std::string& fileName, const bool flipUVs)
{
    SDL_Log("Working dir: %s", std::filesystem::current_path().string().c_str());
    unsigned int flags = aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_CalcTangentSpace;
    if (flipUVs)
        flags |= aiProcess_FlipUVs;

    const aiScene* scene = importer.ReadFile(fileName, flags);
    SDL_Log("Assimp error: %s", importer.GetErrorString());

    return scene;
}

bool AssetLoader::loadMesh(MeshIdInt id, const aiMesh& asimpMeshResult)
{

    std::cout << "loadMesh id:" << id << std::endl;
    Asset::Mesh& mesh = Asset::meshes_[id];

    mesh.indexData_.reserve(asimpMeshResult.mNumFaces);
    mesh.vertexData_.reserve(asimpMeshResult.mNumVertices);
    const bool hasTangents = asimpMeshResult.HasTangentsAndBitangents();
    if (!hasTangents && !g_loggedMissingTangents) {
        SDL_Log(
            "AssetLoader: mesh tangents missing after aiProcess_CalcTangentSpace; normal maps will use vertex normals");
        g_loggedMissingTangents = true;
    }

    for (unsigned int i = 0; i < asimpMeshResult.mNumVertices; i++) {
        Asset::Vertex v{};

        aiVector3D& aiV_i = asimpMeshResult.mVertices[i];
        aiVector3D& aiN_i = asimpMeshResult.mNormals[i];
        // aiVector3D& aiT0_i = asimpMeshResult.mTextureCoords[0][i];

        v.position = glm::vec3(aiV_i.x, aiV_i.y, aiV_i.z);

        v.normal = glm::vec3(aiN_i.x, aiN_i.y, aiN_i.z);

        // v.texUV = glm::vec2(aiT0_i.x, aiT0_i.y);
        if (asimpMeshResult.mTextureCoords[0]) {
            v.texUV = glm::vec2(asimpMeshResult.mTextureCoords[0][i].x, asimpMeshResult.mTextureCoords[0][i].y);
        } else {
            v.texUV = glm::vec2(0.0f, 0.0f);
        }

        if (hasTangents) {
            const glm::vec3 tangent{
                asimpMeshResult.mTangents[i].x, asimpMeshResult.mTangents[i].y, asimpMeshResult.mTangents[i].z};
            const glm::vec3 bitangent{
                asimpMeshResult.mBitangents[i].x, asimpMeshResult.mBitangents[i].y, asimpMeshResult.mBitangents[i].z};
            const float handedness = glm::dot(glm::cross(v.normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
            const float tangentLength = glm::length(tangent);
            v.tangent =
                glm::vec4(tangentLength > 0.00001f ? tangent / tangentLength : glm::vec3(1.0f, 0.0f, 0.0f), handedness);
        } else {
            v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        }

        mesh.vertexData_.push_back(v);
    }

    for (unsigned int i = 0; i < asimpMeshResult.mNumFaces; i++) {
        aiFace& aiF_i = asimpMeshResult.mFaces[i];
        [[maybe_unused]] const unsigned int aiF_i_NumVertices = aiF_i.mNumIndices;
        unsigned int* aiF_i_Indices = aiF_i.mIndices;

        assert(aiF_i_NumVertices == 3);
        assert(aiF_i_Indices != nullptr);

        mesh.indexData_.push_back(aiF_i_Indices[0]);
        mesh.indexData_.push_back(aiF_i_Indices[1]);
        mesh.indexData_.push_back(aiF_i_Indices[2]);
    }

    computeMeshAABB(id);
    return true;
}

bool readAiColor(aiMaterial& material, const char* key, unsigned int type, unsigned int index, glm::vec3& out)
{
    aiColor3D color;
    if (material.Get(key, type, index, color) != AI_SUCCESS)
        return false;

    out = {color.r, color.g, color.b};
    return true;
}

static bool hasMetadataKey(const aiNode& node, const std::string& keyToFind)
{
    if (!node.mMetaData) {
        return false;
    }

    for (unsigned int i = 0; i < node.mMetaData->mNumProperties; i++) {
        if (std::string(node.mMetaData->mKeys[i].C_Str()) == keyToFind) {
            return true;
        }
    }

    return false;
}

static bool isWeaponMountPointName(const std::string& nodeName)
{
    return nodeName.rfind("ik_", 0) == 0 || nodeName.rfind("socket_", 0) == 0 || nodeName == "is_muzzle";
}

static bool isCollisionOnlyName(const std::string& name)
{
    return name.rfind("COL_", 0) == 0;
}

glm::quat rotationFromTransform(const glm::mat4& transform)
{
    glm::mat3 basis(transform);
    for (int axis = 0; axis < 3; ++axis) {
        const float len = glm::length(basis[axis]);
        basis[axis] = len > 0.00001f ? basis[axis] / len : glm::vec3(axis == 0, axis == 1, axis == 2);
    }
    return glm::normalize(glm::quat_cast(basis));
}

void AssetLoader::computeMeshAABB(const MeshIdInt id)
{
    Asset::Mesh& mesh = Asset::meshes_.at(id);

    for (Asset::Vertex v: mesh.vertexData_) {
       mesh.aabb_.min = glm::min(mesh.aabb_.min,glm::vec4(v.position,1.0f));
       mesh.aabb_.max = glm::max(mesh.aabb_.max,glm::vec4(v.position,1.0f));
    }

}
bool AssetLoader::loadModel(const ModelIdInt id,
                            const std::string& modelFileName,
                            const std::vector<std::string>& /*texFileNames*/,
                            const bool k_flatten,
                            const bool flipUVs)
{
    Assimp::Importer importer;
    std::string debugPrefix = "Static Model Loading: ";
    std::string assetIdNameSpace = modelFileName + "::";

    // std::cout << " Asset::Model &newModel = Asset::models_[id]; " << std::endl;
    Asset::Model& newModel = Asset::models_[id];
    newModel.pointLights.clear();

    /////////////////////////////////////////////////// LOAD AISCENE ///////////////////////////////////////////////////
    // std::cout << "loadAsset" << std::endl;
    const aiScene* asimpSceneStructurePtr = loadAsset(importer, modelFileName, flipUVs);

    if (asimpSceneStructurePtr == nullptr) {
        std::cout << debugPrefix << "scene is null" << std::endl;
        return false;
    }
    // std::cout << "loadedAsset" << std::endl;

    if (asimpSceneStructurePtr->mNumMeshes == 0 || asimpSceneStructurePtr->mMeshes == nullptr) {
        std::cout << debugPrefix << "model not found" << std::endl;
        return false;
    }

    if (k_flatten) {
        asimpSceneStructurePtr = importer.ApplyPostProcessing(aiProcess_PreTransformVertices); /// FLATTEN AISCENE
    }
    const aiScene& sceneAi = *asimpSceneStructurePtr;
    /////////////////////////////////////////////////// LOAD AISCENE ///////////////////////////////////////////////////
    newModel.modelElements_.reserve(sceneAi.mNumMeshes);

    std::unordered_map<std::string, const aiLight*> pointLightsByNodeName;
    for (unsigned int i = 0; i < sceneAi.mNumLights; ++i) {
        const aiLight* light = sceneAi.mLights[i];
        if (light && light->mType == aiLightSource_POINT)
            pointLightsByNodeName.emplace(light->mName.C_Str(), light);
    }

    std::cout << debugPrefix << "loadMeshes" << std::endl;

    std::stack<const aiNode*> sceneAiDFSStack;
    sceneAiDFSStack.push(sceneAi.mRootNode); // Note: if clang gives an error under push, that is a clang bug

    std::stack<uint32_t> nodeTraversalStack;
    nodeTraversalStack.push(0); // Note: if clang gives an error under push, that is a clang bug

    std::stack<glm::mat4> transformStack;
    transformStack.push(glm::mat4(1.0f));

    Asset::ModelNode modelRootNode;
    newModel.modelNodes_.push_back(modelRootNode);

    ///////////////////////////////////////////////////////////////////////////////////////////////
    while (!sceneAiDFSStack.empty()) {
        const aiNode* currentNodePtr = sceneAiDFSStack.top();
        const aiNode& currentNode = *currentNodePtr;
        sceneAiDFSStack.pop();
        std::cout << "Node: " << currentNode.mName.C_Str() << " | Meshes: " << currentNode.mNumMeshes
                  << " | Children: " << currentNode.mNumChildren << std::endl;

        const uint32_t currentModelNodeIndex = nodeTraversalStack.top();
        nodeTraversalStack.pop();

        glm::mat4 parentTransform = transformStack.top();
        transformStack.pop();

        const std::string nodeName(currentNode.mName.C_Str());

        // // Skip if collider or gameplay entity
        if (hasMetadataKey(currentNode, "entity_type") || hasMetadataKey(currentNode, "is_collision") ||
            isCollisionOnlyName(nodeName)) {
            continue;
        }

        glm::mat4 localTransform = glmFromAiTransform(currentNode.mTransformation);
        glm::mat4 nodeModelTransform = parentTransform * localTransform;

        const glm::vec3 nodeModelPos = glm::vec3(nodeModelTransform * glm::vec4(0, 0, 0, 1));
        if (const auto lightIt = pointLightsByNodeName.find(nodeName); lightIt != pointLightsByNodeName.end()) {
            const aiLight& light = *lightIt->second;
            const glm::vec3 diffuse{light.mColorDiffuse.r, light.mColorDiffuse.g, light.mColorDiffuse.b};
            const float intensity = std::max({diffuse.r, diffuse.g, diffuse.b, 1.0f});
            newModel.pointLights.push_back(Asset::PointLight{
                .position = nodeModelPos,
                .intensity = intensity,
                .color = diffuse / intensity,
                .range = k_defaultPointLightRange,
            });
        }

        if (isWeaponMountPointName(nodeName)) {
            newModel.mountPoints[nodeName] =
                Asset::MountPoint{.position = nodeModelPos, .rotation = rotationFromTransform(nodeModelTransform)};
            SDL_Log("AssetLoader: found weapon mount '%s' at local pos %.2f %.2f %.2f",
                    currentNode.mName.C_Str(),
                    static_cast<double>(nodeModelPos.x),
                    static_cast<double>(nodeModelPos.y),
                    static_cast<double>(nodeModelPos.z));
        }

        if (hasMetadataKey(currentNode, "is_muzzle") || nodeName == "socket_muzzle" || nodeName == "is_muzzle") {
            newModel.hasMuzzle = true;
            newModel.muzzleLocalPos = nodeModelPos;
            SDL_Log("AssetLoader: found muzzle on node '%s' at local pos %.2f %.2f %.2f",
                    currentNode.mName.C_Str(),
                    static_cast<double>(newModel.muzzleLocalPos.x),
                    static_cast<double>(newModel.muzzleLocalPos.y),
                    static_cast<double>(newModel.muzzleLocalPos.z));
        }

        Asset::ModelNode& currentModelNode = newModel.modelNodes_[currentModelNodeIndex];
        currentModelNode.transform_ = localTransform;
        currentModelNode.childIndices_.reserve(currentNode.mNumChildren);

        pushAiNodeMeshesToModelElements(assetIdNameSpace, currentNode, sceneAi, id, currentModelNodeIndex);

        for (unsigned int i = 0; i < currentNode.mNumChildren; i++) {
            sceneAiDFSStack.push(currentNode.mChildren[i]);
            transformStack.push(nodeModelTransform);

            Asset::ModelNode newNode_i;
            newModel.modelNodes_.push_back(newNode_i);
            uint32_t childNodeIndex = static_cast<uint32_t>(newModel.modelNodes_.size() - 1);
            nodeTraversalStack.push(childNodeIndex);
            newModel.modelNodes_[currentModelNodeIndex].childIndices_.push_back(
                childNodeIndex); // Can't use existing reference because it may be invalidated when std::vector
                                 // reallocates
        }
    }
    ///////////////////////////////////////////////////////////////////////////////////////////////
    std::cout << "modelElements_ count: " << Asset::models_[id].modelElements_.size() << std::endl;

    updateModelTransformCache(id);

    std::cout << debugPrefix << "loadedMeshes" << std::endl;

    if (asimpSceneStructurePtr->HasMaterials()) {
        newModel.materialsByIndex_.reserve(asimpSceneStructurePtr->mNumMaterials);
        for (unsigned int i = 0; i < asimpSceneStructurePtr->mNumMaterials; i++) {
            aiMaterial* mat = asimpSceneStructurePtr->mMaterials[i];
            MaterialIdInt matId = Asset::getMaterialIdFromString(assetIdNameSpace + "material_" + std::to_string(i));
            newModel.materialsByIndex_.push_back(matId);

            Asset::Material& mat_ = Asset::materials_[matId];

            mat_.hasPhongData_ |= readAiColor(*mat, AI_MATKEY_COLOR_DIFFUSE, mat_.kDiffuse_);
            mat_.hasPhongData_ |= readAiColor(*mat, AI_MATKEY_COLOR_AMBIENT, mat_.kAmbient_);
            mat_.hasPhongData_ |= readAiColor(*mat, AI_MATKEY_COLOR_SPECULAR, mat_.kSpecular_);
            mat_.hasPhongData_ |= readAiColor(*mat, AI_MATKEY_COLOR_EMISSIVE, mat_.kEmission_);

            // glTF stores the base color as a linear factor (AI_MATKEY_BASE_COLOR);
            // older Assimp builds don't always alias it onto COLOR_DIFFUSE. Read it
            // explicitly so flat-color parts (e.g. the R-301 magazine's orange) keep
            // their authored colour instead of falling back to the default grey.
            aiColor4D baseColor;
            if (mat->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
                mat_.kDiffuse_ = {baseColor.r, baseColor.g, baseColor.b};
                mat_.hasPhongData_ = true;
            }

            float shininess = 0.0f;
            if (mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
                mat_.nSpecular = shininess;
                mat_.hasPhongData_ = true;
            }

            float ior = 0.0f;
            if (mat->Get(AI_MATKEY_REFRACTI, ior) == AI_SUCCESS) {
                mat_.nIor = ior;
                mat_.hasPhongData_ = true;
            }

            aiString texPath;
            const bool hasBaseColor = getMaterialTexturePath(*mat, aiTextureType_BASE_COLOR, texPath);
            if (hasBaseColor || getMaterialTexturePath(*mat, aiTextureType_DIFFUSE, texPath)) {
                mat_.diffuseTexture = loadEmbeddedMaterialTexture(*asimpSceneStructurePtr, assetIdNameSpace, texPath);
            }

            if (getMaterialTexturePath(*mat, aiTextureType_NORMALS, texPath))
                mat_.normalTexture = loadEmbeddedMaterialTexture(*asimpSceneStructurePtr, assetIdNameSpace, texPath);

            aiString metalPath;
            aiString roughnessPath;
            aiString unknownPath;
            const bool hasMetal = getMaterialTexturePath(*mat, aiTextureType_METALNESS, metalPath);
            const bool hasRoughness = getMaterialTexturePath(*mat, aiTextureType_DIFFUSE_ROUGHNESS, roughnessPath);
            const bool hasUnknown = getMaterialTexturePath(*mat, aiTextureType_UNKNOWN, unknownPath);

            if (hasMetal && hasRoughness && std::string(metalPath.C_Str()) == std::string(roughnessPath.C_Str())) {
                mat_.metallicRoughnessTexture =
                    loadEmbeddedMaterialTexture(*asimpSceneStructurePtr, assetIdNameSpace, metalPath);
            } else if (hasMetal) {
                mat_.metallicRoughnessTexture =
                    loadEmbeddedMaterialTexture(*asimpSceneStructurePtr, assetIdNameSpace, metalPath);
            } else if (hasRoughness) {
                mat_.metallicRoughnessTexture =
                    loadEmbeddedMaterialTexture(*asimpSceneStructurePtr, assetIdNameSpace, roughnessPath);
            } else if (hasUnknown) {
                mat_.metallicRoughnessTexture =
                    loadEmbeddedMaterialTexture(*asimpSceneStructurePtr, assetIdNameSpace, unknownPath);
            }

            // Synthesize a 1x1 solid-color diffuse texture for a material that has a
            // base/diffuse color but no albedo map (e.g. the R-301 magazine's flat
            // 'assault_mag_orange', which also has no UVs). The unified per-mesh
            // texture path needs a texture to bind for every mesh; without one those
            // parts fall back to another mesh's atlas and sample its unused, fully
            // transparent (0,0) texel — making the part vanish in-game even though
            // its material alpha is 1. A dedicated opaque texel renders the true
            // colour. The factor is linear (glTF), so sRGB-encode it before upload
            // (the diffuse upload format is *_SRGB → the GPU decodes it back).
            if (mat_.diffuseTexture == 0 && mat_.hasPhongData_) {
                const TexIdInt solidId =
                    Asset::getTexIdFromString(assetIdNameSpace + "solidcolor_" + std::to_string(i));
                Asset::Texture& solid = Asset::textures_[solidId];
                if (solid.tex == nullptr && solid.tex_raw == nullptr) {
                    const auto encodeSrgb = [](float c) {
                        c = std::clamp(c, 0.0f, 1.0f);
                        const float e = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
                        return static_cast<unsigned char>(std::lround(e * 255.0f));
                    };
                    auto* pixel = static_cast<stbi_uc*>(std::malloc(4));
                    if (pixel) {
                        pixel[0] = encodeSrgb(mat_.kDiffuse_.r);
                        pixel[1] = encodeSrgb(mat_.kDiffuse_.g);
                        pixel[2] = encodeSrgb(mat_.kDiffuse_.b);
                        pixel[3] = 255;
                        solid.tex_raw = pixel;
                        solid.width = 1;
                        solid.height = 1;
                        solid.channels = 4;
                    }
                }
                if (solid.tex_raw != nullptr || solid.tex != nullptr)
                    mat_.diffuseTexture = solidId;
            }
        }
    }

    std::cout << debugPrefix << "loadedTexture" << std::endl;

    return true;
}

void AssetLoader::pushAiNodeMeshesToModelElements(const std::string& meshNameSpace,
                                                  const aiNode& nodeAi,
                                                  const aiScene& sceneAi,
                                                  const ModelIdInt k_modelId,
                                                  const uint32_t currentModelNodeIndex)
{
    std::string debugPrefix = "pushAiNodeMeshes: ";
    Asset::Model& newModel = Asset::models_[k_modelId];

    Asset::ModelNode& currentModelNode = newModel.modelNodes_[currentModelNodeIndex];

    const aiString nodeAiName = nodeAi.mName;
    const std::string nodeNameStr(nodeAiName.C_Str());

    for (unsigned int j = 0; j < nodeAi.mNumMeshes; j++) {
        uint32_t mesh_j_IdAi = nodeAi.mMeshes[j];
        if (mesh_j_IdAi >= sceneAi.mNumMeshes) {
            std::cout << debugPrefix << "given aiNode is not in aiScene!!!:" << std::endl;
            std::cout << "\tmesh: " << mesh_j_IdAi << " is not in aiScene's mesh array" << std::endl;
        }
    }

    for (unsigned int j = 0; j < nodeAi.mNumMeshes; j++) {
        Asset::ModelElement me_j;
        uint32_t mesh_j_IdAi = nodeAi.mMeshes[j];

        const aiMesh& mesh_j_Ai = *sceneAi.mMeshes[mesh_j_IdAi];
        if (isCollisionOnlyName(nodeNameStr) || isCollisionOnlyName(mesh_j_Ai.mName.C_Str())) {
            SDL_Log("AssetLoader: skipping collision-only render mesh node='%s' mesh='%s'",
                    nodeNameStr.c_str(),
                    mesh_j_Ai.mName.C_Str());
            continue;
        }

        MaterialIdInt matId =
            Asset::getMaterialIdFromString(meshNameSpace + "material_" + std::to_string(mesh_j_Ai.mMaterialIndex));

        me_j.materialId_ = matId;

        std::string meshName = meshNameSpace + nodeNameStr + std::to_string(mesh_j_IdAi);
        MeshIdInt meshNameId = Asset::getMeshIdFromString(meshName);
        std::cout << "meshName: " << meshName << " | meshNameId: " << meshNameId << std::endl;

        me_j.meshId_ = meshNameId;
        me_j.cachedTransform_ = glm::mat4(1.0f);

        loadMesh(meshNameId, mesh_j_Ai);

        Asset::Mesh &mesh = Asset::meshes_.at(meshNameId);
        me_j.cachedAabb_ = mesh.aabb_;


        newModel.modelElements_.push_back(me_j);
        uint32_t childModelElementIndex = static_cast<uint32_t>(newModel.modelElements_.size() - 1);
        currentModelNode.modelElementsIndices_.push_back(childModelElementIndex);
    }
}
void AssetLoader::updateModelTransformCache(const ModelIdInt id)
{

    uint32_t rootModelNodeIndex = 0;
    Asset::Model& newModel = Asset::models_[id];

    std::stack<glm::mat4> transformStack;
    std::stack<uint32_t> modelNodeIdStack;

    transformStack.push(glm::mat4(1.0f));
    modelNodeIdStack.push(rootModelNodeIndex);

    while (!modelNodeIdStack.empty()) {
        glm::mat4 transform_i = transformStack.top();
        transformStack.pop();

        uint32_t currentModelNodeIndex = modelNodeIdStack.top();
        modelNodeIdStack.pop();

        Asset::ModelNode& currentModelNode = newModel.modelNodes_[currentModelNodeIndex];

        glm::mat4 worldTransform = transform_i * currentModelNode.transform_;

        for (uint32_t childModelNodeIndex : currentModelNode.childIndices_) {
            transformStack.push(worldTransform);
            modelNodeIdStack.push(childModelNodeIndex);
        };

        for (uint32_t modelElementIndex : currentModelNode.modelElementsIndices_) {
            Asset::ModelElement& modelElement_i = newModel.modelElements_[modelElementIndex];
            modelElement_i.cachedTransform_ = worldTransform;
            Asset::Mesh &mesh = Asset::meshes_.at(modelElement_i.meshId_);
            modelElement_i.cachedAabb_ = rigidTransformAABB(mesh.aabb_,worldTransform);
        }
    }
};

Asset::AABB AssetLoader::rigidTransformAABB(const Asset::AABB &aabb, const glm::mat4 &rigidTransform)
{
    Asset::AABB transformedAABB;

    glm::vec3 corners[8] {
        {aabb.min.x, aabb.min.y, aabb.min.z},
        {aabb.min.x, aabb.min.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.min.z},
        {aabb.min.x, aabb.max.y, aabb.max.z},
        {aabb.max.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.max.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.max.z},
    };

    for (auto corner : corners) {
       glm::vec4 transformedCorner = rigidTransform * glm::vec4(corner,1.0f);
       transformedAABB.min = glm::min(transformedAABB.min,transformedCorner);
       transformedAABB.max = glm::max(transformedAABB.max,transformedCorner);
    }

    return transformedAABB;
}

glm::mat4 AssetLoader::glmFromAiTransform(const aiMatrix4x4& transformAi)
{
    glm::mat4 retTransform;

    retTransform[0] = glm::vec4(transformAi.a1, transformAi.b1, transformAi.c1, transformAi.d1);
    retTransform[1] = glm::vec4(transformAi.a2, transformAi.b2, transformAi.c2, transformAi.d2);
    retTransform[2] = glm::vec4(transformAi.a3, transformAi.b3, transformAi.c3, transformAi.d3);
    retTransform[3] = glm::vec4(transformAi.a4, transformAi.b4, transformAi.c4, transformAi.d4);

    return retTransform;
}
