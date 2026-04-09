#include "ModelLoader.hpp"

#include <SDL3/SDL_log.h>

// Assimp headers trigger several warnings that are not our code to fix.
// Suppress them cleanly rather than disabling whole-file warnings.
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#endif
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

bool loadModel(const std::string& path, std::vector<MeshData>& outMeshes)
{
    Assimp::Importer importer;

    // aiProcess_Triangulate     — convert quads / n-gons to triangles
    // aiProcess_GenSmoothNormals — generate normals if the mesh lacks them
    // aiProcess_JoinIdenticalVertices — de-duplicate vertices
    // aiProcess_FlipUVs         — flip V coordinate (OpenGL → SDL3 GPU convention)
    const aiScene* scene =
        importer.ReadFile(path,
                          static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                                    aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs));

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        SDL_Log("ModelLoader: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    outMeshes.reserve(scene->mNumMeshes);

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh->HasPositions() || mesh->mNumFaces == 0)
            continue;

        MeshData data;
        data.vertices.reserve(mesh->mNumVertices);

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            ModelVertex vert{};
            vert.position = {mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z};

            if (mesh->HasNormals())
                vert.normal = {mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z};

            if (mesh->mTextureCoords[0] != nullptr)
                vert.texCoord = {mesh->mTextureCoords[0][v].x, mesh->mTextureCoords[0][v].y};

            data.vertices.push_back(vert);
        }

        data.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int i = 0; i < face.mNumIndices; ++i)
                data.indices.push_back(face.mIndices[i]);
        }

        outMeshes.push_back(std::move(data));
    }

    SDL_Log("ModelLoader: loaded '%s' — %u mesh(es)", path.c_str(), static_cast<unsigned>(outMeshes.size()));
    return !outMeshes.empty();
}
