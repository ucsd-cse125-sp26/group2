/// @file TriMeshCollision.hpp
/// @brief Triangle-mesh collision with BVH acceleration and Voronoi welding.
///
/// Provides a BVH builder + welding pass (called once at load time) and
/// swept-AABB / depenetration queries against triangle meshes.  The BVH is a
/// flat-array binary tree where each leaf holds up to 4 triangles.  Welding
/// data on the mesh drives Voronoi-region contact clipping at runtime — see
/// `WorldTriMesh` for the data layout.

#pragma once

#include "SweptCollision.hpp"

#include <glm/glm.hpp>

namespace physics
{

/// @brief Build the BVH for a WorldTriMesh.
///
/// Must be called after `vertices` and `indices` are populated.  Fills in
/// `bvhNodes`, `triIndices`, `boundsMin`, and `boundsMax`.  Does NOT populate
/// the welding data — call `weldTriMesh()` after this.
void buildTriMeshBVH(WorldTriMesh& mesh);

/// @brief Compute face normals + active edge / vertex flags for a triangle mesh.
///
/// Must be called AFTER `buildTriMeshBVH()` (it only depends on `vertices` and
/// `indices`, but conventionally pairs with the BVH build).  Populates the
/// mesh's `faceNormals`, `edgeActive`, and `vertActive` arrays.
///
/// **Algorithm.** Each shared edge between two triangles is classified by
/// dihedral angle:
///   - *Convex* edges (folds outward) with angle > `coplanarTolerance` are
///     marked active — these are real corners contacts should fire on.
///   - *Concave* and *coplanar* edges are marked inactive ("welded"), so
///     the runtime depenetration discards Voronoi-region contacts on them.
/// Boundary edges (used by exactly one triangle) and non-manifold edges
/// (>2 triangles) are always active.
/// Vertex flags are derived: a vertex is active in a triangle iff one of
/// the two incident edges in that triangle is active.
///
/// @param mesh                Triangle mesh to weld (modified in place).
/// @param coplanarTolerance   Dihedral angle (radians) below which an edge
///                            counts as flat.  Default ~2° — matches Bullet's
///                            internal-edge utility and Jolt's MeshShape
///                            default.
void weldTriMesh(WorldTriMesh& mesh, float coplanarTolerance = 0.0349065850f /* 2° */);

/// @brief Sweep an AABB against a triangle mesh using Voronoi-clipped per-triangle tests.
///
/// Returns the earliest contact whose closest feature on the hit triangle is
/// an *active* Voronoi region (face interior or active edge / vertex).
/// Contacts in inactive regions are discarded — the neighbour triangle whose
/// active region actually owns that point will produce the correct face-normal
/// contact instead.
HitResult sweepAABBvsTriMesh(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldTriMesh& mesh);

/// @brief Push an AABB out of a triangle mesh using Voronoi-clipped face-normal MTVs.
///
/// For each triangle the AABB overlaps (BVH-accelerated leaf search), the
/// closest feature is found and Voronoi-clipped against the mesh's
/// active-edge/active-vertex flags.  Surviving contacts contribute one face-
/// normal MTV (depth × face normal); contributions across multiple triangles
/// are aggregated and applied once.
///
/// This replaces the older SAT-MTV implementation, which fired on internal
/// edges of triangulated planar surfaces ("ghost contacts") and required
/// 4 averaging passes to mask the noise.  With Voronoi clipping, ghosts are
/// suppressed at the source — one pass suffices.
///
/// Velocity is updated to cancel the component flowing into the surface, so
/// the entity slides along the contact rather than re-penetrating next frame.
///
/// @param pos          AABB centre — modified in place to push out of overlaps.
/// @param vel          Velocity — modified in place to cancel inward motion.
/// @param halfExtents  AABB half-extents.
/// @param mesh         Triangle mesh to depenetrate from (must be welded).
/// @param pushback     Tiny extra distance added to each push to avoid hairline
///                     contact (matches Quake's DIST_EPSILON of 1/32 unit).
void depenetrateAABBvsTriMesh(
    glm::vec3& pos, glm::vec3& vel, glm::vec3 halfExtents, const WorldTriMesh& mesh, float pushback = 0.03125f);

} // namespace physics
