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

#include <cstdint>
#include <glm/glm.hpp>

namespace physics
{

/// @brief Voronoi region of a triangle.  Identifies which feature (face,
/// one of three edges, or one of three vertices) a closest-point query
/// landed on.  Used by depenetration / sweep / closest-point queries that
/// need to clip contacts against the cooked welding-active masks, and by
/// the Phase D wallrun manifold walk to decide whether an edge crossing
/// hops to a neighbour triangle.
enum class TriRegion : uint8_t
{
    Face = 0,
    Edge0 = 1, ///< Edge v0 → v1
    Edge1 = 2, ///< Edge v1 → v2
    Edge2 = 3, ///< Edge v2 → v0
    Vert0 = 4,
    Vert1 = 5,
    Vert2 = 6,
};

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
///
/// For capsule shapes (Phase 5), pass `halfExtents = (radius, radius + halfHeight, radius)`
/// — the resulting Minkowski hull is conservative but exact for axis-aligned
/// face normals (the most common case for hand-authored maps).  Truly
/// diagonal triangle face normals (e.g. a 45° ramp triangle) produce a
/// slight `(sqrt(2)-1)*radius` over-estimate of player size.
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

/// @brief Sweep a capsule against a triangle mesh.  Capsule analogue of
/// `sweepAABBvsTriMesh` for Phase A of physics-future-path.md.
///
/// Replaces the AABB's anisotropic Minkowski extent (which couples to wall
/// orientation and produced the standoff bug at MovementSystem.cpp:761-815)
/// with the capsule's isotropic-plus-axis extent `r = radius + halfHeight *
/// |dot(up, n)|`.  On any horizontal wall normal, the half-extent is just
/// `radius` — no dependency on which way the wall faces in the XZ plane.
HitResult sweepCapsuleVsTriMesh(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldTriMesh& mesh);

/// @brief Push a capsule out of a triangle mesh.  Capsule analogue of
/// `depenetrateAABBvsTriMesh` — identical iteration scheme (4 passes,
/// per-tick budget cap, velocity-coherent + welded-feature guards) with
/// the capsule Minkowski extent in place of the AABB sum.
void depenetrateCapsuleVsTriMesh(
    glm::vec3& pos, glm::vec3& vel, CapsuleShape capsule, const WorldTriMesh& mesh, float pushback = 0.03125f);

/// @brief Result of a closest-point-on-mesh query.  Phase B foundation for
/// the Phase D wallrun manifold walk.
///
/// When `found` is true, `dist` is the unsigned distance from the query
/// segment to `pointOnMesh`, which lies in the indicated `region` of
/// triangle `triId`.  `normal` is the face normal of that triangle,
/// oriented so it points toward the query segment (away from the solid).
struct ClosestPointOnMeshResult
{
    bool found{false};
    float dist{1e30f};
    glm::vec3 pointOnSegment{0.0f};
    glm::vec3 pointOnMesh{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    uint32_t triId{UINT32_MAX};
    TriRegion region{TriRegion::Face};
};

/// @brief Find the closest point on the mesh's surface to a query segment,
/// considering only points within `maxDist`.  BVH-accelerated.
///
/// For wallrun (Phase D) the query segment is the capsule's inner axis
/// (`capsule.segA(pos)` to `capsule.segB(pos)`).  The result identifies
/// "what wall am I on" without the heuristic dot-product reassignment
/// that the current sphere-cast-based WallDetection uses.  At edges and
/// vertices, `region` plus `triId` plus the mesh's `edgeNeighbor` array
/// is enough to walk the surface manifold across triangle seams.
///
/// Returns `found = false` if no triangle is within `maxDist`.
ClosestPointOnMeshResult closestPointOnMesh(
    glm::vec3 segA, glm::vec3 segB, float maxDist, const WorldTriMesh& mesh);

/// @brief Convenience overload — uses the capsule's inner axis as the query
/// segment at the given centre position.
ClosestPointOnMeshResult closestPointOnMesh(
    CapsuleShape capsule, glm::vec3 center, float maxDist, const WorldTriMesh& mesh);

} // namespace physics
