/// @file MapLoader.hpp
/// @brief Load collision geometry (and optionally visual data) from a map GLB file.
///
/// Maps are authored in Blender and exported as `.glb`.  Collision geometry is
/// identified by **Blender collection hierarchy**: meshes whose Assimp scene-graph
/// ancestor is named after the collision collection (default `"Collision"`) are
/// extracted as physics primitives.  Everything else is treated as visual-only.
///
/// **Prototype mode** (`allMeshesAreCollision = true`): every mesh in the file is
/// used for *both* rendering and collision.  Handy for blockout maps where the
/// visual geometry is already simple enough to collide against.
///
/// Each collision mesh is **auto-detected** as the best-fitting primitive:
///   sphere → cylinder → axis-aligned box → convex brush (fallback).
/// Sub-collections (`Boxes/`, `Cylinders/`, `Spheres/`, `Brushes/`) can override
/// the auto-detection to force a specific type.

#pragma once

#include "SweptCollision.hpp"

#include <string>
#include <vector>

struct LoadedModel; // Forward-declared; defined in client/renderer/ModelLoader.hpp.

namespace physics
{

// ---------------------------------------------------------------------------
// Collision data
// ---------------------------------------------------------------------------

/// @brief Owns the collision primitives extracted from a map file.
///
/// The vectors own their memory; `geometry()` returns lightweight spans into
/// them, matching the `WorldGeometry` expected by the collision / movement /
/// raycast systems.
struct MapCollisionData
{
    std::vector<Plane> planes;
    std::vector<WorldAABB> boxes;
    std::vector<WorldBrush> brushes;
    std::vector<WorldCylinder> cylinders;
    std::vector<WorldSphere> spheres;
    std::vector<WorldTriMesh> triMeshes;

    /// @brief Return a non-owning `WorldGeometry` view into this data.
    ///
    /// The returned spans are valid for as long as the vectors are not
    /// reallocated (i.e. as long as the `MapCollisionData` is alive and
    /// no further push_backs occur).
    [[nodiscard]] WorldGeometry geometry() const
    {
        return {.planes = planes,
                .boxes = boxes,
                .brushes = brushes,
                .cylinders = cylinders,
                .spheres = spheres,
                .triMeshes = triMeshes};
    }
};

// ---------------------------------------------------------------------------
// Load options
// ---------------------------------------------------------------------------

/// @brief Configuration for map loading.
struct MapLoadOptions
{
    /// Uniform scale applied to every vertex position (e.g. 39.37 for m → in).
    float scale = 1.0f;

    /// Name of the Blender collection (= Assimp parent node) whose children
    /// are collision geometry.  Matching is case-insensitive.  Meshes under
    /// this node are extracted as collision primitives and are **excluded**
    /// from the visual model (unless `allMeshesAreCollision` is also set).
    std::string collisionCollection = "Collision";

    /// When true, **every** mesh in the file is treated as both visual and
    /// collision geometry.  The `collisionCollection` name is ignored.
    /// Ideal for prototype / blockout maps whose geometry is already simple.
    bool allMeshesAreCollision = false;

    /// When true, an infinite floor plane is added at the lowest Y coordinate
    /// found across all collision geometry.  Prevents players from falling
    /// through the world even if the map mesh has tiny cracks.
    bool addFloorPlane = false;
};

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/// @brief Extract collision geometry from a map `.glb` file.
///
/// Walks the Assimp scene graph.  For each mesh node, determines whether it
/// belongs to the collision collection (by checking ancestor node names) or,
/// in prototype mode, always.  Collision meshes are converted to per-object
/// axis-aligned bounding boxes.
///
/// This function does **not** produce visual / renderable data — use the
/// existing `Renderer::loadSceneModel()` path for that.
///
/// @param path  Absolute or relative path to the `.glb` file.
/// @param out   Filled with extracted collision primitives on success.
/// @param opts  Loading options (scale, collection name, prototype mode).
/// @return True on success; false on any Assimp load error (logged via SDL_Log).
bool loadMapCollision(const std::string& path, MapCollisionData& out, const MapLoadOptions& opts = {});

/// @brief Load collision for a standalone prop GLB and append to existing collision data.
///
/// Loads the GLB, applies the given transform (position + uniform scale), runs
/// auto-detection on each mesh, and appends the resulting primitives to `out`.
/// Call `setActiveWorld(out.geometry())` after all props are loaded to update
/// the physics world.
///
/// @param path     Absolute path to the `.glb` file.
/// @param out      Existing collision data to append to.
/// @param position World-space position of the prop.
/// @param scale    Uniform scale factor.
/// @return True on success.
bool loadPropCollision(const std::string& path, MapCollisionData& out, glm::vec3 position, float scale);

} // namespace physics
