//
// Created by mysteriousjim on 4/16/2026.
//

#include "AssetLoader.hpp"
#include <vector>

const aiScene *AssetLoader::loadAsset(const std::string& fileName)
{
    Assimp::Importer importer;

    const aiScene *scene = importer.ReadFile(fileName,
                                       aiProcess_Triangulate
                                           | aiProcess_GenNormals
                                           | aiProcess_FlipUVs );
    return scene;
}

bool AssetLoader::loadMesh(MeshIdInt id ,const aiMesh &asimpMeshResult)
{
    Asset::Mesh &mesh = Asset::meshes_.at(id);

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
    Asset::Model &newModel = Asset::models_[id];

    const aiScene *asimpSceneStructure = loadAsset(modelFileName);

    if (asimpSceneStructure->mNumMeshes == 0 ||
        asimpSceneStructure->mMeshes == nullptr) {
        return false;
    }

    if (asimpSceneStructure->mMeshes[0] == nullptr) return false;

    aiMesh& asimpMeshResult = *asimpSceneStructure->mMeshes[0];

    MeshIdInt newModelMeshId = 0;
    newModel.meshId_ = loadMesh(newModelMeshId,asimpMeshResult);

    return true;
}
