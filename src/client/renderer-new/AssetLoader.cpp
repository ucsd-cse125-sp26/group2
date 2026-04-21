//
// Created by mysteriousjim on 4/16/2026.
//

#include "AssetLoader.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

const aiScene *AssetLoader::loadAsset(Assimp::Importer &importer,const std::string& fileName)
{
    SDL_Log("Working dir: %s", std::filesystem::current_path().string().c_str());
    SDL_Log("Assimp error: %s", importer.GetErrorString());
    const aiScene *scene = importer.ReadFile(fileName,
                                       aiProcess_Triangulate
                                           | aiProcess_GenNormals
                                           | aiProcess_FlipUVs );

    return scene;
}

bool AssetLoader::loadModelsList()
{
    const ModelIdInt id = 0;
    std::string modelFileName = "test_model.obj";
    const std::vector<std::string> texFileNames;

    std::cout << "loading model" << std::endl;
    loadModel(id,modelFileName,texFileNames);
    std::cout << "loaded model" << std::endl;

    return true;
}

bool AssetLoader::loadMesh(MeshIdInt id ,const aiMesh &asimpMeshResult)
{
    Asset::Mesh &mesh = Asset::meshes_[id];


    mesh.indexData_.reserve(asimpMeshResult.mNumFaces);
    mesh.vertexData_.reserve(asimpMeshResult.mNumVertices);

    for (unsigned int i = 0; i < asimpMeshResult.mNumVertices; i++) {
        Asset::Vertex v{};

        aiVector3D &aiV_i = asimpMeshResult.mVertices[i];
        aiVector3D &aiN_i = asimpMeshResult.mNormals[i];
        aiVector3D &aiT0_i = asimpMeshResult.mTextureCoords[0][i];

        v.position = glm::vec3(aiV_i.x,
                               aiV_i.y,
                               aiV_i.z);

        v.normal = glm::vec3(aiN_i.x,
                             aiN_i.y,
                             aiN_i.z);

        v.texUV = glm::vec2(aiT0_i.x,
                            aiT0_i.y);

        mesh.vertexData_.push_back(v);
    }

    for (unsigned int i = 0; i < asimpMeshResult.mNumFaces; i++) {
        aiFace &aiF_i = asimpMeshResult.mFaces[i];
        unsigned int &aiF_i_NumVertices = aiF_i.mNumIndices;
        unsigned int *aiF_i_Indices = aiF_i.mIndices;

        assert(aiF_i_NumVertices == 3);
        assert(aiF_i_Indices != nullptr);

        mesh.indexData_.push_back(aiF_i_Indices[0]);
        mesh.indexData_.push_back(aiF_i_Indices[1]);
        mesh.indexData_.push_back(aiF_i_Indices[2]);
    }

    return true;
}

bool AssetLoader::loadModel(const ModelIdInt id,const std::string& modelFileName,const std::vector<std::string>& texFileNames)
{
    Assimp::Importer importer;

    std::cout << " Asset::Model &newModel = Asset::models_[id]; " << std::endl;
    Asset::Model &newModel = Asset::models_[id];

    std::cout << "loadAsset" << std::endl;
    const aiScene *asimpSceneStructure = loadAsset(importer, modelFileName);

    if (asimpSceneStructure == nullptr ) {
        std::cout << "scene is null" << std::endl;
        return false;
    }
    std::cout << "loadedAsset" << std::endl;

    if (asimpSceneStructure->mNumMeshes == 0 ||
        asimpSceneStructure->mMeshes == nullptr) {
        std::cout << "scene not found" << std::endl;
        return false;
    }

    std::cout << " if (asimpSceneStructure->mMeshes[0] == nullptr) return false; " << std::endl;
    if (asimpSceneStructure->mMeshes[0] == nullptr) return false;

    aiMesh& asimpMeshResult = *asimpSceneStructure->mMeshes[0];

    MeshIdInt newModelMeshId = 0;
    std::cout << "loadMesh" << std::endl;
    newModel.meshId_ = loadMesh(newModelMeshId,asimpMeshResult) ? newModelMeshId : -1;
    std::cout << "loadedMesh" << std::endl;

    return true;
}
