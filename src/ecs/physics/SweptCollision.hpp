/// @file SweptCollision.hpp
/// @brief Swept AABB and sphere collision queries against world geometry.

#pragma once

#include <glm/vec3.hpp>
#include <span>

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
    glm::vec3 normal; ///< Unit vector pointing into free (non-solid) space.
    float distance;   ///< Signed offset: `dot(normal, p) == distance` for points on the plane.
};

/// @brief An axis-aligned box in world space, used as static collision geometry.
struct WorldAABB
{
    glm::vec3 min; ///< Minimum corner (lowest x, y, z).
    glm::vec3 max; ///< Maximum corner (highest x, y, z).
};

/// @brief A convex volume defined by bounding planes (for ramps, angled walls, etc.).
///
/// The solid interior is the intersection of all half-spaces: `dot(normal, p) < distance`.
/// Normals point outward (into free space), same as the Plane convention.
struct WorldBrush
{
    static constexpr int k_maxPlanes = 8;
    Plane planes[k_maxPlanes];
    int planeCount{0};
};

/// @brief All world collision geometry for one tick.
struct WorldGeometry
{
    std::span<const Plane> planes;
    std::span<const WorldAABB> boxes;
    std::span<const WorldBrush> brushes;
};

/// @brief Result of a swept AABB collision query.
struct HitResult
{
    bool hit{false};                    ///< True if the sweep intersected a plane.
    float tFirst{1.0f};                 ///< Fraction along the movement path [0..1] where the first hit occurs.
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; ///< Surface normal at the contact point.
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

/// @brief Sweep an AABB against all world geometry, returning the earliest hit.
HitResult sweepAll(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldGeometry& world);

// Sphere cast

/// @brief Result of a sphere-cast query (includes world-space hit point).
struct SphereHitResult
{
    bool hit{false};
    float t{1.0f};                      ///< Fraction along path [0..1].
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; ///< Surface normal at contact.
    glm::vec3 point{0.0f};              ///< World-space contact point on the surface.
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
