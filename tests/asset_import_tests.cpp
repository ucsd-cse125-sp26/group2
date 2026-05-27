#define STB_IMAGE_IMPLEMENTATION
#include "client/renderer-new/AssetLoader.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cassert>
#include <filesystem>
#include <iostream>

#ifndef GROUP2_SOURCE_DIR
#define GROUP2_SOURCE_DIR "."
#endif

namespace
{

bool hasTextureOfType(const aiMaterial& material, aiTextureType type)
{
    aiString texturePath;
    return material.GetTexture(type, 0, &texturePath) == AI_SUCCESS;
}

bool hasRenderableTexture(const aiScene& scene)
{
    for (unsigned int i = 0; i < scene.mNumMaterials; ++i) {
        const aiMaterial* material = scene.mMaterials[i];
        if (material != nullptr && (hasTextureOfType(*material, aiTextureType_BASE_COLOR) ||
                                    hasTextureOfType(*material, aiTextureType_DIFFUSE)))
        {
            return true;
        }
    }

    return false;
}

void assertAssimpLoads(const char* relativeAssetPath)
{
    const std::filesystem::path path = std::filesystem::path(GROUP2_SOURCE_DIR) / relativeAssetPath;

    Assimp::Importer importer;
    const aiScene* scene =
        importer.ReadFile(path.string(), static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_GenNormals));

    if (!scene || !scene->mRootNode || scene->mNumMeshes == 0) {
        std::cerr << relativeAssetPath << ": " << importer.GetErrorString() << '\n';
        assert(false && "asset failed to load through Assimp");
    }

    assert(scene->mNumMaterials > 0);
    if (!hasRenderableTexture(*scene)) {
        std::cerr << relativeAssetPath << ": no base-color/diffuse texture exposed through Assimp\n";
        assert(false && "asset loaded but did not expose a renderable material texture");
    }
}

void assertRendererLoaderLoads(const char* relativeAssetPath)
{
    const std::filesystem::path path = std::filesystem::path(GROUP2_SOURCE_DIR) / relativeAssetPath;
    const ModelIdInt modelId = Asset::getModelIdFromString(relativeAssetPath);

    assert(AssetLoader::loadModel(modelId, path.string(), {}, false, false));
    assert(Asset::models_.at(modelId).modelElements_.size() > 0);

    bool hasDecodedTexture = false;
    for (const auto& [id, texture] : Asset::textures_) {
        (void)id;
        if (texture.tex_raw != nullptr && texture.width > 0 && texture.height > 0 && texture.channels > 0) {
            hasDecodedTexture = true;
            break;
        }
    }

    assert(hasDecodedTexture);
}

} // namespace

int main()
{
    assertAssimpLoads("assets/suzanne_substance.usdz");
    assertAssimpLoads("assets/suzanne_substance_v2.glb");
    assertRendererLoaderLoads("assets/suzanne_substance.usdz");
    assertRendererLoaderLoads("assets/suzanne_substance_v2.glb");
    return 0;
}
