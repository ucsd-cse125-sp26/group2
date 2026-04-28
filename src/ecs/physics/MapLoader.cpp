/// @file MapLoader.cpp
/// @brief Assimp-based collision extraction from map GLB files.

#include "MapLoader.hpp"

#include <SDL3/SDL_log.h>

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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

namespace physics
{

namespace
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// @brief Convert an Assimp row-major 4×4 matrix to a GLM column-major matrix.
glm::mat4 aiToGlm(const aiMatrix4x4& m)
{
    return glm::transpose(glm::make_mat4(&m.a1));
}

/// @brief Case-insensitive substring check.
bool containsCI(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    auto toLower = [](const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
        return out;
    };
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

/// @brief Walk up the Assimp node hierarchy to check if any ancestor's name
///        matches the collision collection name (case-insensitive).
///
/// In Blender, when you parent objects under an empty named "Collision" and
/// export to glTF, that empty becomes a node in the scene graph.  This
/// function detects that lineage.
bool isUnderCollectionNode(const aiNode* node, const std::string& collectionName)
{
    // Walk upward from the node (skip the node itself — it's the mesh node).
    const aiNode* cur = node->mParent;
    while (cur != nullptr) {
        if (containsCI(std::string(cur->mName.C_Str()), collectionName))
            return true;
        cur = cur->mParent;
    }
    return false;
}

/// @brief Accumulate the world transform from root to this node.
glm::mat4 accumulatedTransform(const aiNode* node)
{
    glm::mat4 t(1.0f);
    // Collect transforms from root to node.
    // Walk up to root, collect, then multiply in order.
    std::vector<const aiNode*> chain;
    const aiNode* cur = node;
    while (cur != nullptr) {
        chain.push_back(cur);
        cur = cur->mParent;
    }
    // Multiply root-to-leaf.
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        t = t * aiToGlm((*it)->mTransformation);
    }
    return t;
}

/// @brief Extract an AABB from a single mesh, transformed by its world matrix and
///        the user-specified uniform scale.
/// @param mesh   Assimp mesh.
/// @param world  Accumulated node transform (root → node).
/// @param scale  Uniform scale (e.g. 39.37 for m → in).
/// @param outBox Filled with the axis-aligned bounding box.
/// @return True if the mesh had at least one vertex.
bool extractAABB(const aiMesh* mesh, const glm::mat4& world, float scale, WorldAABB& outBox)
{
    if (!mesh->HasPositions() || mesh->mNumVertices == 0)
        return false;

    glm::vec3 bmin(1e30f);
    glm::vec3 bmax(-1e30f);

    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
        const glm::vec4 local(mesh->mVertices[v].x, mesh->mVertices[v].y, mesh->mVertices[v].z, 1.0f);
        const glm::vec3 worldPos = glm::vec3(world * local) * scale;

        bmin = glm::min(bmin, worldPos);
        bmax = glm::max(bmax, worldPos);
    }

    outBox.min = bmin;
    outBox.max = bmax;
    return true;
}

/// @brief Recursively walk the scene graph and extract collision AABBs.
///
/// @param node             Current Assimp node.
/// @param scene            Assimp scene.
/// @param collectionName   Name of the collision collection node.
/// @param allAreCollision  Prototype mode: treat every mesh as collision.
/// @param scale            Uniform scale.
/// @param out              Output collision data.
void extractCollision(const aiNode* node,
                      const aiScene* scene,
                      const std::string& collectionName,
                      bool allAreCollision,
                      float scale,
                      MapCollisionData& out)
{
    // Determine if this node (or an ancestor) belongs to the collision collection.
    const bool isCollision = allAreCollision || isUnderCollectionNode(node, collectionName) ||
                             containsCI(std::string(node->mName.C_Str()), collectionName);

    if (isCollision) {
        const glm::mat4 world = accumulatedTransform(node);

        for (unsigned int mi = 0; mi < node->mNumMeshes; ++mi) {
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
            WorldAABB box{};
            if (extractAABB(mesh, world, scale, box)) {
                out.boxes.push_back(box);
                SDL_Log("MapLoader: collision AABB [%.1f,%.1f,%.1f] → [%.1f,%.1f,%.1f] (node '%s')",
                        static_cast<double>(box.min.x),
                        static_cast<double>(box.min.y),
                        static_cast<double>(box.min.z),
                        static_cast<double>(box.max.x),
                        static_cast<double>(box.max.y),
                        static_cast<double>(box.max.z),
                        node->mName.C_Str());
            }
        }
    }

    // Recurse into children.
    for (unsigned int c = 0; c < node->mNumChildren; ++c)
        extractCollision(node->mChildren[c], scene, collectionName, allAreCollision, scale, out);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool loadMapCollision(const std::string& path, MapCollisionData& out, const MapLoadOptions& opts)
{
    Assimp::Importer importer;

    const unsigned int flags = static_cast<unsigned int>(aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

    const aiScene* scene = importer.ReadFile(path, flags);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        SDL_Log("MapLoader: failed to load '%s': %s", path.c_str(), importer.GetErrorString());
        return false;
    }

    out.planes.clear();
    out.boxes.clear();
    out.brushes.clear();

    extractCollision(scene->mRootNode, scene, opts.collisionCollection, opts.allMeshesAreCollision, opts.scale, out);

    if (out.boxes.empty()) {
        SDL_Log("MapLoader: WARNING — no collision geometry extracted from '%s'", path.c_str());
        // Still return true — the file loaded; there just happened to be no matching meshes.
    }

    // Optionally add an infinite floor plane at the lowest Y across all boxes.
    if (opts.addFloorPlane && !out.boxes.empty()) {
        float lowestY = 1e30f;
        for (const WorldAABB& box : out.boxes)
            lowestY = std::min(lowestY, box.min.y);

        out.planes.push_back(Plane{.normal = {0.0f, 1.0f, 0.0f}, .distance = lowestY});
        SDL_Log("MapLoader: added floor plane at y=%.1f", static_cast<double>(lowestY));
    }

    SDL_Log("MapLoader: loaded '%s' — %zu plane(s), %zu box(es), %zu brush(es)",
            path.c_str(),
            out.planes.size(),
            out.boxes.size(),
            out.brushes.size());

    return true;
}

} // namespace physics
