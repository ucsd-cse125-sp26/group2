/// @file SweptCollision.hpp
/// @brief Swept AABB and sphere collision queries against world geometry.

#pragma once

#include "SurfaceType.hpp"

#include <cmath>
#include <cstdint>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <span>
#include <vector>

/// @brief Pure swept-collision math — no ECS types, no registry.
///
/// **Plane convention:** `dot(normal, p) > distance` is free space;
/// `dot(normal, p) < distance` is solid. The normal always points into free space.
///
/// Example planes (Y-up coordinate system):
/// - Floor at y=0:               `{ normal=(0,1,0),  distance=0    }`
/// - Ceiling at y=512:           `{ normal=(0,-1,0), distance=-512 }`
/// - Wall at x=256 (solid right): `{ normal=(-1,0,0), distance=-256 }`
namespace physics
{

/// @brief An infinite plane dividing free space from solid geometry.
struct Plane
{
    glm::vec3 normal;                              ///< Unit vector pointing into free (non-solid) space.
    float distance;                                ///< Signed offset: `dot(normal, p) == distance` for points on the plane.
    SurfaceType surfaceType = SurfaceType::Concrete; ///< Material tag for impact VFX / SFX (Phase 3).
};

/// @brief An axis-aligned box in world space, used as static collision geometry.
struct WorldAABB
{
    glm::vec3 min;                                   ///< Minimum corner (lowest x, y, z).
    glm::vec3 max;                                   ///< Maximum corner (highest x, y, z).
    SurfaceType surfaceType = SurfaceType::Concrete; ///< Material tag (Phase 3).
};

/// @brief A convex volume defined by bounding planes (for ramps, angled walls, etc.).
///
/// The solid interior is the intersection of all half-spaces: `dot(normal, p) < distance`.
/// Normals point outward (into free space), same as the Plane convention.
///
/// `k_maxPlanes` is sized to fit the typical output of V-HACD convex
/// decomposition: each output hull has up to ~64 vertices (the V-HACD
/// `m_maxNumVerticesPerCH` parameter we pass), and a triangulated convex
/// polytope's face-plane count is bounded by ~2V-4 for V vertices but in
/// practice (after coplanar-triangle deduplication) much lower.  64 planes
/// is comfortable headroom and keeps each `WorldBrush` at 1KB — well within
/// L1 cache for the per-frame collision loop.
struct WorldBrush
{
    static constexpr int k_maxPlanes = 64;
    Plane planes[k_maxPlanes];
    int planeCount{0};
    SurfaceType surfaceType = SurfaceType::Concrete; ///< Material tag (Phase 3).
};

/// @brief A vertical (Y-axis) cylinder in world space.
///
/// Defined by a base centre point, radius, and height.  The cylinder extends
/// from `base.y` to `base.y + height` along the Y axis.
struct WorldCylinder
{
    glm::vec3 base;                                  ///< Centre of the bottom cap.
    float radius;                                    ///< Horizontal radius.
    float height;                                    ///< Extent along +Y from base.
    SurfaceType surfaceType = SurfaceType::Concrete; ///< Material tag (Phase 3).
};

/// @brief A sphere in world space.
struct WorldSphere
{
    glm::vec3 center;                                ///< Centre point.
    float radius;                                    ///< Radius.
    SurfaceType surfaceType = SurfaceType::Concrete; ///< Material tag (Phase 3).
};

/// @brief A single BVH node for spatial acceleration of triangle meshes.
struct BVHNode
{
    glm::vec3 boundsMin; ///< AABB minimum corner.
    glm::vec3 boundsMax; ///< AABB maximum corner.
    int leftFirst;       ///< If leaf: index into triIndices[]. If interior: left child index.
    int count;           ///< >0 → leaf with `count` triangles.  0 → interior node.
};

/// @brief A triangle mesh with BVH acceleration for collision queries.
///
/// Built once at load time via `buildTriMeshBVH()`.  The BVH is a flat array
/// binary tree; leaves hold up to 4 triangles.  `triIndices` is a permutation
/// array mapping BVH leaf ranges to triangle indices in `indices`.
///
/// **Phase 2 welding data** (`faceNormals`, `edgeActive`, `vertActive`):
/// produced by `weldTriMesh()` after `buildTriMeshBVH()`.  Drives Voronoi-
/// region contact clipping in the runtime collision primitives: per-triangle
/// `edgeActive` bits mark genuine boundary edges; `vertActive` marks corners
/// touched by an active edge.  Internal edges (welded coplanar / concave
/// edges shared between adjacent triangles) are cleared so the depenetration
/// path discards ghost contacts on them, matching the behaviour of Havok /
/// Bullet / Jolt mesh shapes.
struct WorldTriMesh
{
    std::vector<glm::vec3> vertices;  ///< All vertex positions (world space, scaled).
    std::vector<uint32_t> indices;    ///< Triangle indices (3 per triangle).
    std::vector<BVHNode> bvhNodes;    ///< Flat BVH node array.
    std::vector<uint32_t> triIndices; ///< Permutation: BVH leaf ranges → triangle indices.
    glm::vec3 boundsMin{0.0f};        ///< Whole-mesh AABB min.
    glm::vec3 boundsMax{0.0f};        ///< Whole-mesh AABB max.

    // Welding data (one entry per canonical triangle index in `indices`).
    std::vector<glm::vec3> faceNormals; ///< CCW face normal (unit length) per triangle.
    std::vector<uint8_t> edgeActive;    ///< Bit i set ⇔ edge i of this triangle is an active (boundary or convex) edge.
    std::vector<uint8_t> vertActive;    ///< Bit i set ⇔ vertex i of this triangle is touched by an active edge.

    // Phase 3 material data.  If `triangleMaterials` is empty, every triangle
    // falls back to `defaultSurface`.  Authored by `MapLoader` from Blender
    // per-face materials.
    std::vector<uint8_t> triangleMaterials;            ///< One `SurfaceType` index per triangle (empty = use default).
    SurfaceType defaultSurface = SurfaceType::Concrete; ///< Fallback for triangles without per-face material data.
};

/// @brief All world collision geometry for one tick.
struct WorldGeometry
{
    std::span<const Plane> planes;
    std::span<const WorldAABB> boxes;
    std::span<const WorldBrush> brushes;
    std::span<const WorldCylinder> cylinders;
    std::span<const WorldSphere> spheres;
    std::span<const WorldTriMesh> triMeshes;
};

/// @brief Capsule shape input for swept-collision queries.
///
/// A capsule is a line segment thickened by a radius.  The segment endpoints
/// are `center ± up * halfHeight`; the surface is every point within `radius`
/// of the segment.  `up` defaults to +Y but may be any unit vector — this
/// supports gravity-flipped play (where the capsule axis is -Y) and any
/// future arbitrarily-oriented characters without changing the query API.
///
/// Functions taking a `CapsuleShape` treat `center` as the capsule's centre
/// of mass (the midpoint of the segment), matching the semantics of the
/// AABB family that takes a centre + halfExtents.
struct CapsuleShape
{
    float radius{16.0f};                    ///< Cylinder cross-section radius.
    float halfHeight{20.0f};                ///< Half the segment length (excludes spherical caps).
    glm::vec3 up{0.0f, 1.0f, 0.0f};         ///< Unit-length axis direction.

    /// @brief Top endpoint of the inner segment at `center`.
    [[nodiscard]] glm::vec3 segA(glm::vec3 center) const noexcept
    {
        return center + up * halfHeight;
    }

    /// @brief Bottom endpoint of the inner segment at `center`.
    [[nodiscard]] glm::vec3 segB(glm::vec3 center) const noexcept
    {
        return center - up * halfHeight;
    }

    /// @brief Tight half-extents of the AABB that encloses the capsule at `center`.
    ///
    /// Used for BVH broad-phase culling.  For a Y-aligned capsule of radius
    /// `r` and half-height `h`, this returns `(r, h + r, r)`.
    [[nodiscard]] glm::vec3 enclosingHalfExtents() const noexcept
    {
        return {std::abs(up.x) * halfHeight + radius,
                std::abs(up.y) * halfHeight + radius,
                std::abs(up.z) * halfHeight + radius};
    }

    /// @brief Minkowski half-extent of the capsule along a unit direction `n`.
    ///
    /// For a Y-aligned capsule this is `r + h * |n.y|`; for an arbitrary
    /// axis it is `r + h * |dot(up, n)|`.  Used by every plane / slab /
    /// Minkowski-sum query as the capsule analogue of
    /// `|n.x|*hx + |n.y|*hy + |n.z|*hz` for an AABB.
    [[nodiscard]] float minkowskiExtent(glm::vec3 n) const noexcept
    {
        return radius + halfHeight * std::abs(glm::dot(up, n));
    }
};

/// @brief Result of a swept AABB collision query.
struct HitResult
{
    bool hit{false};                    ///< True if the sweep intersected a plane.
    float tFirst{1.0f};                 ///< Fraction along the movement path [0..1] where the first hit occurs.
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; ///< Surface normal at the contact point.
    SurfaceType surfaceType{SurfaceType::Concrete}; ///< Material at the hit surface (Phase 3).
};

/// @brief Sweep an AABB along the path [start, end] against a list of infinite planes.
///
/// Uses the Minkowski-sum approach: each plane is expanded outward by the AABB
/// half-extents, reducing the problem to a ray-vs-expanded-plane intersection.
///
/// @param halfExtents  Half-dimensions of the AABB.
/// @param start        World-space start position (AABB centre).
/// @param end          World-space end position (AABB centre).
/// @param planes       World collision planes to test against.
/// @return             Earliest hit within the sweep, or `HitResult{hit=false}` if the path is clear.
/// @note               Entities that start already inside a plane are skipped.
///                     Depenetration is handled separately by CollisionSystem before calling this.
HitResult sweepAABB(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, std::span<const Plane> planes);

/// @brief Sweep an AABB against a static axis-aligned box.
///
/// Expands the static box by the moving AABB's half-extents (Minkowski sum) and
/// performs a ray-slab intersection test on the swept centre point.
/// Entities starting inside the box are skipped (depenetration handles that).
HitResult sweepAABBvsBox(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldAABB& box);

/// @brief Sweep an AABB against a convex brush (set of bounding planes).
///
/// Finds the time at which the AABB enters all half-spaces simultaneously.
/// Entities starting inside the brush are skipped (depenetration handles that).
HitResult sweepAABBvsBrush(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldBrush& brush);

/// @brief Sweep an AABB against a vertical cylinder.
///
/// Minkowski-expands the cylinder by the AABB half-extents: the radius grows
/// by the XZ extent and the height caps grow by the Y extent.  The sweep then
/// reduces to a 2D ray-vs-circle test (XZ) clamped by the expanded Y slab.
HitResult sweepAABBvsCylinder(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldCylinder& cyl);

/// @brief Sweep an AABB against a sphere.
///
/// Minkowski-expands the sphere radius by the AABB half-extents (approximation:
/// uses the max half-extent component, making it slightly conservative at
/// corners).  Then performs a ray-vs-sphere test on the swept centre.
HitResult sweepAABBvsSphere(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldSphere& sph);

/// @brief Sweep an AABB against all world geometry, returning the earliest hit.
HitResult sweepAll(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldGeometry& world);

// Capsule swept-collision against convex primitives (Phase A of physics-future-path.md).
// EXACT for planes and brushes; CONSERVATIVE (uses capsule's enclosing AABB) for box,
// cylinder, sphere — the dev-arena primitives.  Real map geometry is trimesh and has
// an exact capsule path in TriMeshCollision.hpp.

/// @brief Sweep a capsule along [start, end] against a list of infinite planes.
HitResult sweepCapsuleVsPlanes(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, std::span<const Plane> planes);

/// @brief Sweep a capsule against a static axis-aligned box.  Conservative.
HitResult sweepCapsuleVsBox(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldAABB& box);

/// @brief Sweep a capsule against a convex brush.  Exact (per-plane Minkowski extent).
HitResult sweepCapsuleVsBrush(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldBrush& brush);

/// @brief Sweep a capsule against a vertical cylinder.  Conservative.
HitResult sweepCapsuleVsCylinder(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldCylinder& cyl);

/// @brief Sweep a capsule against a sphere.  Conservative.
HitResult sweepCapsuleVsSphere(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldSphere& sph);

/// @brief Sweep a capsule against all world geometry, returning the earliest hit.
HitResult sweepAll(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldGeometry& world);

// Sphere cast

/// @brief Result of a sphere-cast query (includes world-space hit point).
struct SphereHitResult
{
    bool hit{false};
    float t{1.0f};                      ///< Fraction along path [0..1].
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; ///< Surface normal at contact.
    glm::vec3 point{0.0f};              ///< World-space contact point on the surface.
    SurfaceType surfaceType{SurfaceType::Concrete}; ///< Material at the hit surface (Phase 3).
};

/// @brief Cast a sphere along the path [start, end] against all world geometry.
///
/// Uses the Minkowski-sum approach: geometry is expanded by the sphere radius,
/// then the sweep becomes a point (ray) test against the expanded geometry.
///
/// @param radius  Sphere radius (u).
/// @param start   World-space start of sweep (sphere centre).
/// @param end     World-space end of sweep (sphere centre).
/// @param world   World collision geometry to test against.
/// @return        Earliest hit, or `SphereHitResult{hit=false}` if clear.
SphereHitResult sphereCast(float radius, glm::vec3 start, glm::vec3 end, const WorldGeometry& world);

} // namespace physics
