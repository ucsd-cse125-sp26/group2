//
// Created by mysteriousjim on 4/16/2026.
//

#ifndef GROUP2_MODELLOADER_H
#define GROUP2_MODELLOADER_H
#include "Asset.hpp"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "glm/glm.hpp"

#include <string>
#include <unordered_map>

class AssetLoader
{
public:
    static bool loadModel(ModelIdInt id, const std::string& modelFileName, const std::vector<std::string>& texFileNames,bool k_flatten);
    static bool loadModelsList();
    static void updateModelTransformCache(const ModelIdInt id);

private:
    static bool loadMesh(MeshIdInt id, const aiMesh& asimpMeshResult);
    static const aiScene* loadAsset(Assimp::Importer& importer, const std::string& fileName);
    static glm::mat4 glmFromAiTransform(const aiMatrix4x4& transformAi);
    static void pushAiNodeMeshesToModelElements(const std::string &meshNameSpace,const aiNode &nodeAi,const aiScene &sceneAi,const ModelIdInt k_modelId,const uint32_t modelIndexNode);

};

#endif // GROUP2_MODELLOADER_H
