/// @file AssetLoader.cpp
/// @brief Implementation of AssetLoader — Assimp scene import and mesh extraction.

#include "AssetLoader.hpp"
#include "Asset.hpp"

#include <filesystem>
#include <iostream>
#include <stack>
#include <stb_image.h>
#include <vector>

#include <fstream>
#include <functional>

static void dumpSceneStructureToFile(
    const aiScene* scene,
    const std::string& outputFile,
    const std::string& sourceAssetFile)
{
    if (!scene || !scene->mRootNode) {
        return;
    }

    std::ofstream out(outputFile);

    if (!out.is_open()) {
        return;
    }

    auto indent = [&](int depth) {
        for (int i = 0; i < depth; i++) {
            out << "    ";
        }
    };

    auto printTransform = [&](const aiMatrix4x4& t) {
        out << std::fixed << std::setprecision(3);

        out << "[[" << t.a1 << ", " << t.a2 << ", " << t.a3 << ", " << t.a4 << "],\n"
            << " [" << t.b1 << ", " << t.b2 << ", " << t.b3 << ", " << t.b4 << "],\n"
            << " [" << t.c1 << ", " << t.c2 << ", " << t.c3 << ", " << t.c4 << "],\n"
            << " [" << t.d1 << ", " << t.d2 << ", " << t.d3 << ", " << t.d4 << "]]";
    };

    auto printMetadataValue = [&](const aiMetadataEntry& entry) {
        switch (entry.mType) {
        case AI_BOOL:
            out << (*(bool*)entry.mData ? "true" : "false");
            break;

        case AI_INT32:
            out << *(int32_t*)entry.mData;
            break;

        case AI_UINT64:
            out << *(uint64_t*)entry.mData;
            break;

        case AI_FLOAT:
            out << *(float*)entry.mData;
            break;

        case AI_DOUBLE:
            out << *(double*)entry.mData;
            break;

        case AI_AISTRING:
            out << ((aiString*)entry.mData)->C_Str();
            break;

        default:
            out << "<unsupported metadata type " << entry.mType << ">";
            break;
        }
    };

    auto printMetadata = [&](const aiMetadata* metadata, int depth) {
        if (!metadata || metadata->mNumProperties == 0) {
            indent(depth);
            out << "Metadata: none\n";
            return;
        }

        indent(depth);
        out << "Metadata:\n";

        for (unsigned int i = 0; i < metadata->mNumProperties; i++) {
            indent(depth + 1);
            out << metadata->mKeys[i].C_Str() << " = ";
            printMetadataValue(metadata->mValues[i]);
            out << "\n";
        }
    };

    std::function<void(const aiNode*, int)> recurse;

    recurse = [&](const aiNode* node, int depth) {
        if (!node) {
            return;
        }

        indent(depth);
        out << "Node: " << node->mName.C_Str() << "\n";

        indent(depth);
        out << "Meshes: " << node->mNumMeshes
            << " | Children: " << node->mNumChildren << "\n";

        indent(depth);
        out << "Transform:\n";

        indent(depth + 1);
        printTransform(node->mTransformation);
        out << "\n";

        printMetadata(node->mMetaData, depth);

        if (node->mNumChildren == 0) {
            indent(depth);
            out << "---- LEAF NODE ----\n";

            if (node->mNumMeshes == 0) {
                indent(depth + 1);
                out << "No meshes attached\n";
            }

            for (unsigned int i = 0; i < node->mNumMeshes; i++) {
                unsigned int meshIndex = node->mMeshes[i];

                if (meshIndex >= scene->mNumMeshes) {
                    indent(depth + 1);
                    out << "Mesh[" << meshIndex << "] INVALID INDEX\n";
                    continue;
                }

                const aiMesh* mesh = scene->mMeshes[meshIndex];

                indent(depth + 1);
                out << "Mesh[" << meshIndex << "]\n";

                indent(depth + 2);
                out << "Name: " << mesh->mName.C_Str() << "\n";

                indent(depth + 2);
                out << "Vertices: " << mesh->mNumVertices << "\n";

                indent(depth + 2);
                out << "Faces: " << mesh->mNumFaces << "\n";

                indent(depth + 2);
                out << "Material Index: " << mesh->mMaterialIndex << "\n";

                indent(depth + 2);
                out << "Has Positions: "
                    << (mesh->HasPositions() ? "true" : "false") << "\n";

                indent(depth + 2);
                out << "Has Normals: "
                    << (mesh->HasNormals() ? "true" : "false") << "\n";

                indent(depth + 2);
                out << "Has UV0: "
                    << (mesh->HasTextureCoords(0) ? "true" : "false") << "\n";

                indent(depth + 2);
                out << "Has Tangents: "
                    << (mesh->HasTangentsAndBitangents() ? "true" : "false") << "\n";

                indent(depth + 2);
                out << "Primitive Types: " << mesh->mPrimitiveTypes << "\n";

                if (mesh->mMaterialIndex < scene->mNumMaterials) {
                    aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

                    aiString matName;
                    mat->Get(AI_MATKEY_NAME, matName);

                    indent(depth + 2);
                    out << "Material Name: " << matName.C_Str() << "\n";

                    aiString texPath;

                    if (mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == AI_SUCCESS) {
                        indent(depth + 2);
                        out << "Base Color Texture: " << texPath.C_Str() << "\n";
                    }

                    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
                        indent(depth + 2);
                        out << "Diffuse Texture: " << texPath.C_Str() << "\n";
                    }

                    if (mat->GetTexture(aiTextureType_NORMALS, 0, &texPath) == AI_SUCCESS) {
                        indent(depth + 2);
                        out << "Normal Texture: " << texPath.C_Str() << "\n";
                    }

                    if (mat->GetTexture(aiTextureType_EMISSIVE, 0, &texPath) == AI_SUCCESS) {
                        indent(depth + 2);
                        out << "Emissive Texture: " << texPath.C_Str() << "\n";
                    }

                    if (mat->GetTexture(aiTextureType_METALNESS, 0, &texPath) == AI_SUCCESS) {
                        indent(depth + 2);
                        out << "Metalness Texture: " << texPath.C_Str() << "\n";
                    }

                    if (mat->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texPath) == AI_SUCCESS) {
                        indent(depth + 2);
                        out << "Roughness Texture: " << texPath.C_Str() << "\n";
                    }
                }
            }

            indent(depth);
            out << "-------------------\n";
        }

        out << "\n";

        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            recurse(node->mChildren[i], depth + 1);
        }
    };

    out << "==== ASSIMP SCENE DUMP ====\n\n";

    out << "Source Asset: " << sourceAssetFile << "\n";
    out << "Output File: " << outputFile << "\n\n";

    out << "Meshes: " << scene->mNumMeshes << "\n";
    out << "Materials: " << scene->mNumMaterials << "\n";
    out << "Textures: " << scene->mNumTextures << "\n";
    out << "Animations: " << scene->mNumAnimations << "\n\n";

    recurse(scene->mRootNode, 0);
}

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
        unsigned int& aiF_i_NumVertices = aiF_i.mNumIndices;
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

bool AssetLoader::loadModel(const ModelIdInt id,
                            const std::string& modelFileName,
                            const std::vector<std::string>& texFileNames,
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
    if (modelFileName == "/home/wifu/CLionProjects/CSE125/build/release/assets/maps/map1.glb") {
        dumpSceneStructureToFile(
            asimpSceneStructurePtr,
            "scene_dump.txt",
            modelFileName
        );
    }

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

        Asset::ModelNode& currentModelNode = newModel.modelNodes_[currentModelNodeIndex];
        currentModelNode.transform_ = glmFromAiTransform(currentNode.mTransformation);
        currentModelNode.childIndices_.reserve(currentNode.mNumChildren);

        pushAiNodeMeshesToModelElements(assetIdNameSpace, currentNode, sceneAi, id, currentModelNodeIndex);

        for (int i = 0; i < currentNode.mNumChildren; i++) {
            sceneAiDFSStack.push(currentNode.mChildren[i]);

            Asset::ModelNode newNode_i;
            newModel.modelNodes_.push_back(newNode_i);
            uint32_t childNodeIndex = newModel.modelNodes_.size() - 1;
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
            MaterialIdInt matId =
                Asset::getMaterialIdFromString(assetIdNameSpace + "material_" + std::to_string(i));

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

            TexIdInt texId =
                Asset::getTexIdFromString(assetIdNameSpace + "texture_" + std::string(texPath.C_Str()));

            mat_.texId_[0] = texId;

            Asset::Texture& tex_ = Asset::textures_[texId];

            if (tex_.tex_raw != nullptr) {
                continue; // already decoded this texture
            }

            const aiTexture* embeddedTexture = asimpSceneStructurePtr->GetEmbeddedTexture(texPath.C_Str());
            if (!embeddedTexture || embeddedTexture->mHeight != 0) {
                SDL_Log("AssetLoader: skipping unsupported material texture '%s' "
                        "(expected compressed embedded texture, embedded=%s, height=%u)",
                        texPath.C_Str(),
                        embeddedTexture ? "true" : "false",
                        embeddedTexture ? embeddedTexture->mHeight : 0);
                continue;
            }

            stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(embeddedTexture->pcData),
                                                   static_cast<int>(embeddedTexture->mWidth),
                                                   &tex_.width,
                                                   &tex_.height,
                                                   &tex_.channels,
                                                   4);

            if (!pixels) {
                std::cout << "stbi failed for "
                        << texPath.C_Str()
                        << ": "
                        << stbi_failure_reason()
                        << std::endl;
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

    for (int j = 0; j < nodeAi.mNumMeshes; j++) {
        uint32_t mesh_j_IdAi = nodeAi.mMeshes[j];
        if (mesh_j_IdAi >= sceneAi.mNumMeshes) {
            std::cout << debugPrefix << "given aiNode is not in aiScene!!!:" << std::endl;
            std::cout << "\tmesh: " << mesh_j_IdAi << " is not in aiScene's mesh array" << std::endl;
        }
    }

    for (int j = 0; j < nodeAi.mNumMeshes; j++) {
        Asset::ModelElement me_j;
        uint32_t mesh_j_IdAi = nodeAi.mMeshes[j];

        const aiMesh& mesh_j_Ai = *sceneAi.mMeshes[mesh_j_IdAi];

        MaterialIdInt matId = Asset::getMaterialIdFromString(meshNameSpace + "material_" + std::to_string(mesh_j_Ai.mMaterialIndex));

        me_j.materialId_ = matId;

        std::string meshName = meshNameSpace + nodeNameStr + std::to_string(mesh_j_IdAi);
        MeshIdInt meshNameId = Asset::getMeshIdFromString(meshName);
        std::cout << "meshName: " << meshName << " | meshNameId: " << meshNameId << std::endl;

        me_j.meshId_ = meshNameId;
        me_j.cachedTransform_ = glm::mat4(1.0f);

        loadMesh(meshNameId, mesh_j_Ai);
        newModel.modelElements_.push_back(me_j);
        uint32_t childModelElementIndex = newModel.modelElements_.size() - 1;
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
