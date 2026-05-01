/// @file AssetLoader.cpp
/// @brief Implementation of AssetLoader — Assimp scene import and mesh extraction.

#include "AssetLoader.hpp"

#include <filesystem>
#include <iostream>
#include <stack>
#include <vector>

const aiScene* AssetLoader::loadAsset(Assimp::Importer& importer, const std::string& fileName)
{
    SDL_Log("Working dir: %s", std::filesystem::current_path().string().c_str());
    const aiScene* scene =
        importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs);
    SDL_Log("Assimp error: %s", importer.GetErrorString());

    return scene;
}

bool AssetLoader::loadModelsList()
{
    const ModelIdInt id = 0;
    // std::string modelFileName = "assualtRifleJ.obj";
    std::string modelFileName = "assualtRiflColorTest.glb";
    const std::vector<std::string> texFileNames;

    std::cout << "loading model" << std::endl;
    bool res = loadModel(id, modelFileName, texFileNames, false);
    std::cout << "loaded model" << std::endl;
    if (!res) {
        std::cout << "MODEL NOT FOUND!!" << std::endl;
    }

    return true;
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
        aiVector3D& aiT0_i = asimpMeshResult.mTextureCoords[0][i];

        v.position = glm::vec3(aiV_i.x, aiV_i.y, aiV_i.z);

        v.normal = glm::vec3(aiN_i.x, aiN_i.y, aiN_i.z);

        v.texUV = glm::vec2(aiT0_i.x, aiT0_i.y);

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

bool AssetLoader::loadModel(const ModelIdInt id,
                            const std::string& modelFileName,
                            const std::vector<std::string>& texFileNames,
                            const bool k_flatten)
{
    Assimp::Importer importer;
    std::string debugPrefix = "Static Model Loading: ";
    std::string assetIdNameSpace = modelFileName + "::";

    // std::cout << " Asset::Model &newModel = Asset::models_[id]; " << std::endl;
    Asset::Model& newModel = Asset::models_[id];

    /////////////////////////////////////////////////// LOAD AISCENE ///////////////////////////////////////////////////
    // std::cout << "loadAsset" << std::endl;
    const aiScene* asimpSceneStructurePtr = loadAsset(importer, modelFileName);

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
