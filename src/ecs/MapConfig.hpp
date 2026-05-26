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
///   false → "all-mesh mode": every mesh in the GLB is both visual *and*
///           collision, loaded as authored triangles with no shape guessing.
///           Used for blockout maps like map1.glb.
///   true  → "separated mode": collision-only nodes are tagged in Blender
///           with the prefix `kCollisionPattern` and excluded from the visual
///           model. Used for production maps where collision is hand-authored
///           independently from rendering geometry.
///
/// IMPORTANT: this flag controls *how* the map is processed, not *which* map
/// is loaded. The map filename comes solely from `kMapAsset` in `AssetCatalog.hpp`.
inline constexpr bool k_separatedCollisionMap = false;

/// @brief Substring that identifies collision-only nodes in separated mode.
/// Matched case-insensitively against Assimp node names.
inline constexpr const char* k_collisionPattern = "COL_";

/// @brief Run V-HACD convex decomposition on non-convex prop meshes?
///
/// Production map collision does not use V-HACD. `COL_` map nodes are
/// authored simplified triangle surfaces and stay triangle surfaces at
/// runtime. This switch only exists for non-map prop experimentation.
///
/// Normal builds also compile without V-HACD; this flag only has an effect
/// when CMake is configured with GROUP2_ENABLE_VHACD=ON. When `false`
/// (default), V-HACD is bypassed and non-convex props fall back to triMesh.
/// Disable to:
///   * skip multi-second prop decomposition during iteration,
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
    opts.addFloorPlane = false; // Map geometry provides its own floor.
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

/// @brief Jump pad authored in Blender (entity_type = 3).
/// Optional custom properties: `jump_velocity_x`, `jump_velocity_y`,
/// `jump_velocity_z` (floats, units/s). If omitted, the runtime default
/// in `JumpPad::velocity` is used (typically straight up).
/// Optional `half_extent_x/y/z` set the trigger AABB half-size.
struct JumpPadSpawner
{
    glm::vec3 pos{0.0f};
    glm::vec3 velocity{0.0f, 1500.0f, 0.0f};
    glm::vec3 halfExtents{48.0f, 24.0f, 48.0f};
};
inline std::vector<JumpPadSpawner> jumpPadSpawner_;

/// @brief Killzone authored in Blender (entity_type = 4).
/// Optional `half_extent_x/y/z` set the trigger AABB half-size — a
/// lava pit is typically wide and shallow (e.g. 256, 16, 256).
struct KillzoneSpawner
{
    glm::vec3 pos{0.0f};
    glm::vec3 halfExtents{128.0f, 32.0f, 128.0f};
};
inline std::vector<KillzoneSpawner> killzoneSpawner_;

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

/// @brief Read a numeric custom property as float. Blender exports floats
/// as AI_DOUBLE and ints as AI_INT32; either is accepted. Returns
/// `fallback` when the key is missing or has an unsupported type.
inline float getMetadataFloat(const aiMetadata* metadata, const std::string& key, float fallback)
{
    if (metadata == nullptr)
        return fallback;
    for (unsigned int i = 0; i < metadata->mNumProperties; i++) {
        if (key != metadata->mKeys[i].C_Str())
            continue;
        const aiMetadataEntry& entry = metadata->mValues[i];
        switch (entry.mType) {
        case AI_FLOAT:
            return *static_cast<float*>(entry.mData);
        case AI_DOUBLE:
            return static_cast<float>(*static_cast<double*>(entry.mData));
        case AI_INT32:
            return static_cast<float>(*static_cast<int32_t*>(entry.mData));
        default:
            return fallback;
        }
    }
    return fallback;
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
            void* entity_type_ptr = getMetadataValue(node->mMetaData, "entity_type");
            if (entity_type_ptr != nullptr) {
                const int32_t entity_type = *static_cast<int32_t*>(entity_type_ptr);
                const char* const nodeName = node->mName.C_Str();
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
                    void* weapon_type_ptr = getMetadataValue(node->mMetaData, "weapon_type");
                    if (weapon_type_ptr == nullptr) {
                        SDL_Log("Weapon spawner '%s' missing 'weapon_type' metadata — skipping", nodeName);
                        break;
                    }
                    const aiMatrix4x4& t = node->mTransformation;
                    WeaponType weapon_type = static_cast<WeaponType>(*static_cast<int32_t*>(weapon_type_ptr));
                    glm::vec3 pos = glm::vec3(t.a4, t.b4, t.c4) * kMapAsset.loadScale;
                    weaponSpawner_.push_back(WeaponSpawner{.type = weapon_type, .pos = pos});
                    break;
                }
                case 2: // Power up spawn point
                {
                    void* powerup_type_ptr = getMetadataValue(node->mMetaData, "powerup_type");
                    if (powerup_type_ptr == nullptr) {
                        SDL_Log("Powerup spawner '%s' missing 'powerup_type' metadata — skipping", nodeName);
                        break;
                    }
                    const aiMatrix4x4& t = node->mTransformation;
                    PowerupType powerup_type = static_cast<PowerupType>(*static_cast<int32_t*>(powerup_type_ptr));
                    glm::vec3 pos = glm::vec3(t.a4, t.b4, t.c4) * kMapAsset.loadScale;
                    powerupSpawner_.push_back(PowerupSpawner{.type = powerup_type, .pos = pos});
                    break;
                }
                case 3: // Jump pad
                {
                    const aiMatrix4x4& t = node->mTransformation;
                    glm::vec3 pos = glm::vec3(t.a4, t.b4, t.c4) * kMapAsset.loadScale;
                    JumpPadSpawner pad;
                    pad.pos = pos;
                    pad.velocity.x = getMetadataFloat(node->mMetaData, "jump_velocity_x", pad.velocity.x);
                    pad.velocity.y = getMetadataFloat(node->mMetaData, "jump_velocity_y", pad.velocity.y);
                    pad.velocity.z = getMetadataFloat(node->mMetaData, "jump_velocity_z", pad.velocity.z);
                    pad.halfExtents.x = getMetadataFloat(node->mMetaData, "half_extent_x", pad.halfExtents.x);
                    pad.halfExtents.y = getMetadataFloat(node->mMetaData, "half_extent_y", pad.halfExtents.y);
                    pad.halfExtents.z = getMetadataFloat(node->mMetaData, "half_extent_z", pad.halfExtents.z);
                    jumpPadSpawner_.push_back(pad);
                    break;
                }
                case 4: // Killzone (lava pit, void, etc.)
                {
                    const aiMatrix4x4& t = node->mTransformation;
                    glm::vec3 pos = glm::vec3(t.a4, t.b4, t.c4) * kMapAsset.loadScale;
                    KillzoneSpawner kz;
                    kz.pos = pos;
                    kz.halfExtents.x = getMetadataFloat(node->mMetaData, "half_extent_x", kz.halfExtents.x);
                    kz.halfExtents.y = getMetadataFloat(node->mMetaData, "half_extent_y", kz.halfExtents.y);
                    kz.halfExtents.z = getMetadataFloat(node->mMetaData, "half_extent_z", kz.halfExtents.z);
                    killzoneSpawner_.push_back(kz);
                    break;
                }
                default:
                    SDL_Log("Unknown entity_type %d on node '%s' — skipping", entity_type, nodeName);
                    break;
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
