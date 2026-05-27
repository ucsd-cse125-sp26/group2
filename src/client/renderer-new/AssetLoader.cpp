/// @file AssetLoader.cpp
/// @brief Implementation of AssetLoader — Assimp scene import and mesh extraction.

#include "AssetLoader.hpp"

#include "Asset.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <iterator>
#include <stack>
#include <stb_image.h>
#include <vector>

const aiScene* AssetLoader::loadAsset(Assimp::Importer& importer, const std::string& fileName, const bool flipUVs)
{
    SDL_Log("Working dir: %s", std::filesystem::current_path().string().c_str());
    unsigned int flags = aiProcess_Triangulate | aiProcess_GenNormals;
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

namespace
{

bool hasBytes(const std::vector<unsigned char>& bytes, std::size_t offset, std::size_t count)
{
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

std::uint16_t readLe16(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

std::uint32_t readLe32(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

std::string normalizeZipEntryName(std::string name)
{
    for (char& c : name) {
        if (c == '\\')
            c = '/';
    }
    while (name.rfind("./", 0) == 0)
        name.erase(0, 2);
    while (!name.empty() && name.front() == '/')
        name.erase(0, 1);
    return name;
}

bool readStoredZipEntry(const std::filesystem::path& zipPath, std::string entryName, std::vector<unsigned char>& out)
{
    entryName = normalizeZipEntryName(std::move(entryName));

    std::ifstream file(zipPath, std::ios::binary);
    if (!file)
        return false;

    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() < 22)
        return false;

    constexpr std::uint32_t k_eocdSig = 0x06054b50U;
    constexpr std::uint32_t k_centralSig = 0x02014b50U;
    constexpr std::uint32_t k_localSig = 0x04034b50U;
    constexpr std::uint16_t k_storeMethod = 0;

    std::size_t eocd = bytes.size() - 22;
    const std::size_t minEocd = bytes.size() > 22 + 0xffff ? bytes.size() - 22 - 0xffff : 0;
    while (eocd > minEocd && (!hasBytes(bytes, eocd, 4) || readLe32(bytes, eocd) != k_eocdSig))
        --eocd;
    if (!hasBytes(bytes, eocd, 22) || readLe32(bytes, eocd) != k_eocdSig)
        return false;

    const std::uint16_t entryCount = readLe16(bytes, eocd + 10);
    const std::uint32_t centralSize = readLe32(bytes, eocd + 12);
    const std::uint32_t centralOffset = readLe32(bytes, eocd + 16);
    if (!hasBytes(bytes, centralOffset, centralSize))
        return false;

    std::size_t offset = centralOffset;
    for (std::uint16_t i = 0; i < entryCount && hasBytes(bytes, offset, 46); ++i) {
        if (readLe32(bytes, offset) != k_centralSig)
            return false;

        const std::uint16_t compressionMethod = readLe16(bytes, offset + 10);
        const std::uint32_t compressedSize = readLe32(bytes, offset + 20);
        const std::uint32_t uncompressedSize = readLe32(bytes, offset + 24);
        const std::uint16_t nameLen = readLe16(bytes, offset + 28);
        const std::uint16_t extraLen = readLe16(bytes, offset + 30);
        const std::uint16_t commentLen = readLe16(bytes, offset + 32);
        const std::uint32_t localHeaderOffset = readLe32(bytes, offset + 42);

        if (!hasBytes(bytes, offset + 46, nameLen))
            return false;

        const std::string name(reinterpret_cast<const char*>(bytes.data() + offset + 46), nameLen);
        if (normalizeZipEntryName(name) == entryName) {
            if (compressionMethod != k_storeMethod || compressedSize != uncompressedSize)
                return false;
            if (!hasBytes(bytes, localHeaderOffset, 30) || readLe32(bytes, localHeaderOffset) != k_localSig)
                return false;

            const std::uint16_t localNameLen = readLe16(bytes, localHeaderOffset + 26);
            const std::uint16_t localExtraLen = readLe16(bytes, localHeaderOffset + 28);
            const std::size_t dataOffset = localHeaderOffset + 30 + localNameLen + localExtraLen;
            if (!hasBytes(bytes, dataOffset, compressedSize))
                return false;

            out.assign(bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + compressedSize));
            return true;
        }

        offset += 46 + nameLen + extraLen + commentLen;
    }

    return false;
}

stbi_uc* loadTextureFromUsdZip(
    const std::string& modelFileName, const aiString& texturePath, int& width, int& height, int& channels)
{
    const std::filesystem::path modelPath(modelFileName);
    if (modelPath.extension() != ".usdz")
        return nullptr;

    std::vector<unsigned char> textureBytes;
    if (!readStoredZipEntry(modelPath, texturePath.C_Str(), textureBytes))
        return nullptr;

    return stbi_load_from_memory(
        textureBytes.data(), static_cast<int>(textureBytes.size()), &width, &height, &channels, 4);
}

} // namespace

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

glm::quat rotationFromTransform(const glm::mat4& transform)
{
    glm::mat3 basis(transform);
    for (int axis = 0; axis < 3; ++axis) {
        const float len = glm::length(basis[axis]);
        basis[axis] = len > 0.00001f ? basis[axis] / len : glm::vec3(axis == 0, axis == 1, axis == 2);
    }
    return glm::normalize(glm::quat_cast(basis));
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

        // // Skip if collider or gameplay entity
        if (hasMetadataKey(currentNode, "entity_type") || hasMetadataKey(currentNode, "is_collision")) {
            continue;
        }

        glm::mat4 parentTransform = transformStack.top();
        transformStack.pop();

        glm::mat4 localTransform = glmFromAiTransform(currentNode.mTransformation);
        glm::mat4 nodeModelTransform = parentTransform * localTransform;

        const std::string nodeName(currentNode.mName.C_Str());
        const glm::vec3 nodeModelPos = glm::vec3(nodeModelTransform * glm::vec4(0, 0, 0, 1));
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

    if (!k_flatten) {
        updateModelTransformCache(id);
    }

    std::cout << debugPrefix << "loadedMeshes" << std::endl;

    if (asimpSceneStructurePtr->HasMaterials()) {
        for (unsigned int i = 0; i < asimpSceneStructurePtr->mNumMaterials; i++) {
            aiMaterial* mat = asimpSceneStructurePtr->mMaterials[i];
            MaterialIdInt matId = Asset::getMaterialIdFromString(assetIdNameSpace + "material_" + std::to_string(i));

            Asset::Material& mat_ = Asset::materials_[matId];

            mat_.hasPhongData_ |= readAiColor(*mat, AI_MATKEY_COLOR_DIFFUSE, mat_.kDiffuse_);
            mat_.hasPhongData_ |= readAiColor(*mat, AI_MATKEY_COLOR_AMBIENT, mat_.kAmbient_);
            mat_.hasPhongData_ |= readAiColor(*mat, AI_MATKEY_COLOR_SPECULAR, mat_.kSpecular_);
            mat_.hasPhongData_ |= readAiColor(*mat, AI_MATKEY_COLOR_EMISSIVE, mat_.kEmission_);

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
            if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) != AI_SUCCESS &&
                mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) != AI_SUCCESS)
            {
                continue; // no texture for this material
            }

            TexIdInt texId = Asset::getTexIdFromString(assetIdNameSpace + "texture_" + std::string(texPath.C_Str()));

            mat_.texId_[0] = texId;

            Asset::Texture& tex_ = Asset::textures_[texId];

            if (tex_.tex_raw != nullptr) {
                continue; // already decoded this texture
            }

            stbi_uc* pixels = nullptr;
            const aiTexture* embeddedTexture = asimpSceneStructurePtr->GetEmbeddedTexture(texPath.C_Str());
            if (embeddedTexture && embeddedTexture->mHeight == 0) {
                pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(embeddedTexture->pcData),
                                               static_cast<int>(embeddedTexture->mWidth),
                                               &tex_.width,
                                               &tex_.height,
                                               &tex_.channels,
                                               4);
            }

            if (pixels == nullptr)
                pixels = loadTextureFromUsdZip(modelFileName, texPath, tex_.width, tex_.height, tex_.channels);

            if (!pixels) {
                std::cout << "stbi failed for " << texPath.C_Str() << ": " << stbi_failure_reason() << std::endl;
                continue;
            }

            tex_.tex_raw = pixels;
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

        MaterialIdInt matId =
            Asset::getMaterialIdFromString(meshNameSpace + "material_" + std::to_string(mesh_j_Ai.mMaterialIndex));

        me_j.materialId_ = matId;

        std::string meshName = meshNameSpace + nodeNameStr + std::to_string(mesh_j_IdAi);
        MeshIdInt meshNameId = Asset::getMeshIdFromString(meshName);
        std::cout << "meshName: " << meshName << " | meshNameId: " << meshNameId << std::endl;

        me_j.meshId_ = meshNameId;
        me_j.cachedTransform_ = glm::mat4(1.0f);

        loadMesh(meshNameId, mesh_j_Ai);
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
        }
    }
};

glm::mat4 AssetLoader::glmFromAiTransform(const aiMatrix4x4& transformAi)
{
    glm::mat4 retTransform;

    retTransform[0] = glm::vec4(transformAi.a1, transformAi.b1, transformAi.c1, transformAi.d1);
    retTransform[1] = glm::vec4(transformAi.a2, transformAi.b2, transformAi.c2, transformAi.d2);
    retTransform[2] = glm::vec4(transformAi.a3, transformAi.b3, transformAi.c3, transformAi.d3);
    retTransform[3] = glm::vec4(transformAi.a4, transformAi.b4, transformAi.c4, transformAi.d4);

    return retTransform;
}
