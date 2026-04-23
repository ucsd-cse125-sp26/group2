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
    static bool
    loadModel(const ModelIdInt id, const std::string& modelFileName, const std::vector<std::string>& texFileNames);
    static bool loadModelsList();

private:
    static bool loadMesh(MeshIdInt id, const aiMesh& asimpMeshResult);
    static const aiScene* loadAsset(Assimp::Importer& importer, const std::string& fileName);
};

#endif // GROUP2_MODELLOADER_H
