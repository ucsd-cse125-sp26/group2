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
///   1. The "how to load it" toggles (`k_separatedCollisionMap`, `k_guessShapesProcessed`).
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
#include "ecs/physics/MapLoader.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

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
inline constexpr bool k_separatedCollisionMap = false;

/// @brief Substring that identifies collision-only nodes in separated mode.
/// Matched case-insensitively against Assimp node names.
inline constexpr const char* k_collisionPattern = "COL_";

/// @brief Should collision meshes be auto-fit to primitive shapes, or kept
/// as raw triMeshes (the exact artist-authored Blender geometry)?
///
///   true  (default) → run the auto-detection pipeline (AABB → cylinder →
///                     sphere → convex brush → triMesh). Convex meshes
///                     become cheap primitives or brushes; only truly
///                     non-convex meshes fall back to triMesh.
///   false           → trust the artist: every collision mesh stays a
///                     triMesh, vertex-for-vertex.
///
/// Has no effect in prototype mode (`k_separatedCollisionMap = false`):
/// every mesh is collision there, and forcing all of them to triMesh would
/// be prohibitively expensive.
inline constexpr bool k_guessShapesProcessed = true;

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

    if (physics::loadMapCollision(path, out, opts)) {
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
        return true;
    }

    SDL_Log("[%s] WARNING: map collision load failed (%s) — falling back to testWorld()", tag, kMapAsset.filename);
    return false;
}

} // namespace gamemap
