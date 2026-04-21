//
// Created by mysteriousjim on 4/16/2026.
//

#ifndef GROUP2_MODELLOADER_H
#define GROUP2_MODELLOADER_H
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "glm/glm.hpp"

#include <string>
#include <unordered_map>

#include "Asset.hpp"

class AssetLoader {
    public:
        static bool loadModel(const ModelIdInt id,const std::string& modelFileName,const std::vector<std::string>& texFileNames);
    private:
        static bool loadMesh(MeshIdInt id ,const aiMesh &asimpMeshResult);
        static const aiScene *loadAsset(const std::string& fileName);
};



#endif //GROUP2_MODELLOADER_H
