/// @file AssetLoader.hpp
/// @brief Assimp-based model loader for the new renderer pipeline.

#pragma once

#include "Asset.hpp"
#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "glm/glm.hpp"

#include <string>
#include <unordered_map>

/// @brief Loads 3D models via Assimp and populates the global Asset registries.
class AssetLoader
{
public:
    /// @brief Load a single model from file and register it in Asset::models_.
    /// @param id Unique model identifier.
    /// @param modelFileName Path to the model file.
    /// @param texFileNames Paths to associated texture files.
    /// @param k_flatten If true, flatten the scene hierarchy via Assimp pre-transform.
    /// @param flipUVs
    /// @return True on success.
    static bool loadModel(const ModelIdInt id,
                          const std::string& modelFileName,
                          const std::vector<std::string>& texFileNames,
                          const bool k_flatten,
                          const bool flipUVs);

    /// @brief Load the hard-coded default models list.
    /// @return True on success.
    static bool loadModelsList();
    static void updateModelTransformCache(const ModelIdInt id);

private:
    /// @brief Import a single Assimp mesh into the global Asset::meshes_ registry.
    /// @param id Unique mesh identifier.
    /// @param asimpMeshResult The Assimp mesh to import.
    /// @return True on success.
    static bool loadMesh(MeshIdInt id, const aiMesh& asimpMeshResult);

    /// @brief Read a file from disk through Assimp and return the parsed scene.
    /// @param importer Assimp importer instance (owns the scene lifetime).
    /// @param fileName Path to the asset file.
    /// @param flipUVs
    /// @return Pointer to the loaded scene, or nullptr on failure.
    static const aiScene* loadAsset(Assimp::Importer& importer, const std::string& fileName, const bool flipUVs);

    /// @brief Convert an Assimp 4x4 matrix to a glm::mat4.
    /// @param transformAi The Assimp transform matrix.
    /// @return The equivalent glm::mat4.
    static glm::mat4 glmFromAiTransform(const aiMatrix4x4& transformAi);

    /// @brief Recursively extract meshes from an Assimp node and add them as ModelElements.
    /// @param meshNameSpace Namespace prefix for generating unique mesh IDs.
    /// @param nodeAi The current Assimp scene node.
    /// @param sceneAi The full Assimp scene.
    /// @param k_modelId The model ID to attach extracted elements to.
    /// @param modelIndexNode
    static void pushAiNodeMeshesToModelElements(const std::string& meshNameSpace,
                                                const aiNode& nodeAi,
                                                const aiScene& sceneAi,
                                                const ModelIdInt k_modelId,
                                                const uint32_t modelIndexNode);
};
