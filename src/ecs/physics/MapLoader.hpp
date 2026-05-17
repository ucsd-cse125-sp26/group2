/// @file MapLoader.hpp
/// @brief Load collision geometry (and optionally visual data) from a map GLB file.
///
/// Maps are authored in Blender and exported as `.glb`.  Collision geometry is
/// identified by **Blender collection hierarchy**: meshes whose Assimp scene-graph
/// ancestor is named after the collision collection (default `"Collision"`) are
/// extracted as collision geometry. Everything else is treated as visual-only.
///
/// **Prototype mode** (`allMeshesAreCollision = true`): every mesh in the file is
/// used for *both* rendering and collision.  Handy for blockout maps where the
/// visual geometry is already simple enough to collide against.
///
/// Production separated maps load collision meshes as authored triangle
/// surfaces. Primitive guessing and V-HACD are opt-in compatibility paths for
/// prototype/debug content and standalone props.

#pragma once

#include "SweptCollision.hpp"

#include <string>
#include <vector>

namespace physics
{

/// Collision data

/// @brief Owns the collision geometry extracted from a map file.
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
    StaticWorldBroadphase staticBroadphase;

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
                .triMeshes = triMeshes,
                .staticBroadphase = &staticBroadphase};
    }
};

/// Load options

/// @brief Configuration for map loading.
struct MapLoadOptions
{
    /// Uniform scale applied to every vertex position (e.g. 39.37 for m → in).
    float scale = 1.0f;

    /// Name of the Blender collection (= Assimp parent node) whose children
    /// are collision geometry.  Matching is case-insensitive.  Meshes under
    /// this node are extracted as collision geometry and are **excluded**
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

    /// In separated mode (`allMeshesAreCollision = false`), should the loader
    /// auto-detect/guess each collision mesh's best-fitting primitive
    /// (AABB / cylinder / sphere / convex brush), or load it as an authored
    /// triangle mesh?
    ///
    ///   true            — run the legacy auto-detection pipeline
    ///                     (AABB → cylinder → sphere → convex brush → triMesh)
    ///                     plus sub-collection / name forcing.  Convex shapes
    ///                     end up as cheap primitives or brushes; only truly
    ///                     non-convex meshes fall back to triMesh.  Convex
    ///                     primitives are dramatically cheaper at runtime and
    ///                     don't suffer triMesh edge-jitter on contact.
    ///   false (default) — preserve exactly what Blender's collision section
    ///                     contains: every collision mesh becomes a triangle
    ///                     mesh, vertex-for-vertex.  Sub-collection name
    ///                     overrides ("Boxes/", "Cylinders/", …) and Blender
    ///                     primitive-name hints ("Cylinder") are ignored.
    ///                     Use this when the artist has authored exact
    ///                     collision hulls and the loader must not second-
    ///                     guess them.
    ///
    /// Has no effect in prototype mode (`allMeshesAreCollision = true`):
    /// every mesh is collision there, and forcing all of them to triMesh
    /// would be prohibitively expensive.
    bool guessShapesProcessed = false;

    /// In separated mode with shape-guessing on, when a collision mesh is
    /// non-convex (so it can't be a single `WorldBrush`), should the loader
    /// run V-HACD convex decomposition to split it into multiple brushes?
    ///
    ///   true            — non-convex meshes go through V-HACD; the resulting
    ///                     hulls are appended as `WorldBrush`es.  Smoother
    ///                     collision than triMesh (no per-triangle MTV
    ///                     jitter) and cheaper at runtime.  Costs a few
    ///                     hundred milliseconds to a few seconds at *load*
    ///                     time per non-convex mesh, depending on size.
    ///   false (default) — skip decomposition; non-convex meshes fall through
    ///                     to `WorldTriMesh`.
    ///
    /// This is a compatibility/prototype option. Production map collision is
    /// authored as simplified triangle surfaces and should keep this false.
    ///
    /// Has no effect when `guessShapesProcessed = false` or when
    /// `allMeshesAreCollision = true`.
    bool decomposeNonConvex = false;
};

/// API

/// @brief Extract collision geometry from a map `.glb` file.
///
/// Walks the Assimp scene graph.  For each mesh node, determines whether it
/// belongs to the collision collection (by checking ancestor node names) or,
/// in prototype mode, always. In separated production mode with
/// `guessShapesProcessed = false`, collision meshes are preserved as
/// `WorldTriMesh` vertex-for-vertex after Assimp triangulation.
///
/// This function does **not** produce visual / renderable data — use the
/// existing `Renderer::loadSceneModel()` path for that.
///
/// @param path  Absolute or relative path to the `.glb` file.
/// @param out   Filled with extracted collision geometry on success.
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
/// @param decomposeNonConvex
///                 When true, non-convex meshes inside the prop are run through
///                 V-HACD convex decomposition (each becomes a small set of
///                 `WorldBrush`es) instead of falling back to `WorldTriMesh`.
///                 Smoother runtime collision on irregular shapes (a bottle, a
///                 bent metal pallet) at the cost of seconds-per-mesh load time.
///                 Default false because a prop GLB can hold dozens of sub-
///                 meshes and decomposing every one of them blows up startup.
/// @return True on success.
bool loadPropCollision(
    const std::string& path, MapCollisionData& out, glm::vec3 position, float scale, bool decomposeNonConvex = false);

} // namespace physics
