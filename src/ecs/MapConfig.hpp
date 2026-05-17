/// @file MapConfig.hpp
/// @brief Single source of truth for which map is loaded and how, shared by client and server.
///
/// **Why this exists.** The server is authoritative for movement and the client predicts;
/// for prediction to agree with the server, both sides must extract *exactly* the same
/// collision primitives from the *exact* same map file. Previously this code was duplicated
/// in `client/game/Game.cpp` and `server/game/ServerGame.cpp` and the two could silently drift.
/// Now both call `gamemap::loadConfiguredMap()` and read the same constants from this header.
///
/// **What lives here.**
///   1. The production collision-map contract (`k_separatedCollisionMap`, `k_collisionPattern`).
///   2. The collision-pattern substring used in separated mode (`k_collisionPattern`).
///   3. Helpers to build `MapLoadOptions` and resolve the absolute map path.
///   4. `loadConfiguredMap()` — fills a `MapCollisionData` using the configured options.
///
/// **What does NOT live here.** The map *filename* itself. That stays in `AssetCatalog.hpp`
/// (`kMapAsset.filename`) so there is one and only one source of truth for which file is
/// loaded. Flipping the toggles below changes *how* the map is processed, never *which*
/// map. To switch to a different map, edit `kMapAsset` in `AssetCatalog.hpp`.

#pragma once

#include "ecs/AssetCatalog.hpp"
#include "ecs/components/PowerupState.hpp"
#include "ecs/physics/MapLoader.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

namespace gamemap
{

/// @brief Does this map use SEPARATED collision and visual meshes?
///
///   false → "prototype mode": every mesh in the GLB is both visual *and*
///           collision. Used for blockout maps like map1.glb.
///   true  → "separated mode": collision-only nodes are tagged in Blender
///           with the prefix `kCollisionPattern` and excluded from the visual
///           model. Used for production maps where collision is hand-authored
///           independently from rendering geometry.
///
/// IMPORTANT: this flag controls *how* the map is processed, not *which* map
/// is loaded. The map filename comes solely from `kMapAsset` in `AssetCatalog.hpp`.
inline constexpr bool k_separatedCollisionMap = true;

/// @brief Substring that identifies collision-only nodes in separated mode.
/// Matched case-insensitively against Assimp node names.
inline constexpr const char* k_collisionPattern = "COL_";

/// @brief Should separated collision meshes be auto-fit to primitive shapes?
///
/// Production maps keep this false: `COL_` Blender collision nodes are loaded
/// as `WorldTriMesh` vertex-for-vertex after export triangulation. The old
/// auto-detection pipeline remains available only for prototype/debug maps
/// that deliberately opt into primitive guessing.
///
/// Has no effect in prototype mode (`k_separatedCollisionMap = false`):
/// every mesh is collision there, and forcing all of them to triMesh would
/// be prohibitively expensive.
inline constexpr bool k_guessShapesProcessed = false;

/// @brief Run V-HACD convex decomposition on non-convex prop meshes?
///
/// Production map collision does not use V-HACD. `COL_` map nodes are
/// authored simplified triangle surfaces and stay triangle surfaces at
/// runtime. This switch only exists for non-map prop experimentation.
///
/// When `false` (default), V-HACD is bypassed and non-convex props fall
/// back to triMesh.  Disable to:
///   * skip the multi-second V-HACD step during map iteration,
///   * sidestep V-HACD regressions on a particular asset, or
///   * keep parity with bot/headless tooling that doesn't need smooth
///     curved-contact collision.
///
/// `WorldTriMesh` collision is the primary map path. Curved prop contact may
/// still benefit from authored simplified collision or a future dedicated prop
/// solution; V-HACD should not be reintroduced into map loading.
///
/// Call sites that pass `decomposeCollision` to `physics::loadPropCollision`
/// must AND that argument with this flag — see `client/game/Game.cpp`,
/// `server/game/ServerGame.cpp`, and `clientbot/main.cpp`.
inline constexpr bool k_useVhacd = false;

/// @brief Build the `MapLoadOptions` used by both client and server.
///
/// Centralised so the two sides cannot drift. The client also reads
/// `k_separatedCollisionMap` / `k_collisionPattern` directly when deciding
/// which visual meshes to exclude from the renderer.
[[nodiscard]] inline physics::MapLoadOptions makeLoadOptions()
{
    physics::MapLoadOptions opts;
    opts.scale = kMapAsset.loadScale;
    opts.allMeshesAreCollision = !k_separatedCollisionMap;
    if (k_separatedCollisionMap)
        opts.collisionCollection = k_collisionPattern;
    opts.guessShapesProcessed = k_guessShapesProcessed;
    opts.decomposeNonConvex = k_useVhacd; // PR-30: project-wide V-HACD gate.
    opts.addFloorPlane = false;           // Map geometry provides its own floor.
    return opts;
}

/// @brief Resolve the absolute path of the configured map file.
///
/// Reads the filename from `kMapAsset` (in `AssetCatalog.hpp`) — never
/// affected by the load-mode toggles above.
[[nodiscard]] inline std::string mapAbsolutePath()
{
    const char* const base = SDL_GetBasePath();
    return std::string(base ? base : "") + "assets/" + kMapAsset.filename;
}

inline std::vector<glm::vec3> spawnPoints_;

struct WeaponSpawner
{
    WeaponType type;
    glm::vec3 pos;
};
inline std::vector<WeaponSpawner> weaponSpawner_;

struct PowerupSpawner
{
    PowerupType type;
    glm::vec3 pos;
};
inline std::vector<PowerupSpawner> powerupSpawner_;

void traverseNodeTree(const aiNode* node, int depth = 0)
{
    if (node == nullptr) {
        return;
    }

    for (int i = 0; i < depth; i++) {
        std::cout << "  ";
    }

    std::cout << node->mName.C_Str() << "\n";

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        traverseNodeTree(node->mChildren[i], depth + 1);
    }
}

void* getMetadataValue(const aiMetadata* metadata, const std::string& key)
{
    if (metadata == nullptr) {
        return nullptr;
    }

    for (unsigned int i = 0; i < metadata->mNumProperties; i++) {
        if (key == metadata->mKeys[i].C_Str()) {
            return metadata->mValues[i].mData;
        }
    }

    return nullptr;
}

/// @brief Load the configured map's collision into `out`.
///
/// Both client and server call this so they end up with identical primitives
/// (a prerequisite for prediction parity). Logs success/failure with `tag`
/// so each side keeps its `[client]` / `[server]` prefix.
///
/// @param out  Filled with extracted collision primitives on success.
/// @param tag  Short side identifier used in log output ("client" / "server").
/// @return Whatever `physics::loadMapCollision` returns.
inline bool loadConfiguredMap(physics::MapCollisionData& out, const char* tag)
{
    const std::string path = mapAbsolutePath();
    const physics::MapLoadOptions opts = makeLoadOptions();

    if (!physics::loadMapCollision(path, out, opts)) {
        SDL_Log("[%s] WARNING: map collision load failed (%s) — falling back to testWorld()", tag, kMapAsset.filename);
        return false;
    }

    SDL_Log("[%s] map collision loaded (%s): %zu planes, %zu boxes, %zu brushes, %zu cylinders, %zu spheres, "
            "%zu trimeshes",
            tag,
            kMapAsset.filename,
            out.planes.size(),
            out.boxes.size(),
            out.brushes.size(),
            out.cylinders.size(),
            out.spheres.size(),
            out.triMeshes.size());

    // Load gameplay entities from map
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, 0 /* no flags */);

    if (scene == nullptr)
        return false;

    std::function<void(const aiNode*, int)> traverse = [&](const aiNode* node, int depth) {
        if (node == nullptr) {
            return;
        }

        if (node->mMetaData) {
            for (unsigned int i = 0; i < node->mMetaData->mNumProperties; i++) {
                if (std::string(node->mMetaData->mKeys[i].C_Str()) == "entity_type") {
                    int32_t entity_type = *static_cast<int32_t*>(getMetadataValue(node->mMetaData, "entity_type"));
                    switch (entity_type) {
                    case 0: // Player spawn point
                    {
                        const aiMatrix4x4& t = node->mTransformation;
                        glm::vec3 pos = glm::vec3(t.a4, t.b4, t.c4) * kMapAsset.loadScale;
                        spawnPoints_.push_back(pos);
                        break;
                    }
                    case 1: // Weapon spawn point
                    {
                        const aiMatrix4x4& t = node->mTransformation;

                        WeaponType weapon_type = static_cast<WeaponType>(
                            *static_cast<int32_t*>(getMetadataValue(node->mMetaData, "weapon_type")));
                        glm::vec3 pos = glm::vec3(t.a4, t.b4, t.c4) * kMapAsset.loadScale;
                        weaponSpawner_.push_back(WeaponSpawner{.type = weapon_type, .pos = pos});
                        break;
                    }
                    case 2: // Power up spawn point
                    {
                        const aiMatrix4x4& t = node->mTransformation;
                        PowerupType powerup_type = static_cast<PowerupType>(
                            *static_cast<int32_t*>(getMetadataValue(node->mMetaData, "powerup_type")));
                        glm::vec3 pos = glm::vec3(t.a4, t.b4, t.c4) * kMapAsset.loadScale;
                        powerupSpawner_.push_back(PowerupSpawner{.type = powerup_type, .pos = pos});
                        break;
                    }
                    default:
                        SDL_Log("Unknown entity type found: %d", entity_type);
                        break;
                    }
                }
            }
        }

        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            traverse(node->mChildren[i], depth + 1);
        }
    };

    traverse(scene->mRootNode, 0);

    std::ofstream out_file("spawn_points.txt");

    if (out_file.is_open()) {
        out_file << "Spawn Points (" << spawnPoints_.size() << ")\n";

        for (size_t i = 0; i < spawnPoints_.size(); i++) {
            const glm::vec3& p = spawnPoints_[i];

            out_file << i << ": (" << p.x << ", " << p.y << ", " << p.z << ")\n";
        }
    }

    return true;
}

} // namespace gamemap
