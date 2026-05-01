/// @file TriMeshCollision.hpp
/// @brief Triangle-mesh collision with BVH acceleration.
///
/// Provides a BVH builder (called once at load time) and swept-AABB / raycast /
/// depenetration queries against triangle meshes.  The BVH is a flat-array
/// binary tree where each leaf holds up to 4 triangles.

#pragma once

#include "SweptCollision.hpp"

namespace physics
{

/// @brief Build the BVH for a WorldTriMesh.
///
/// Must be called after `vertices` and `indices` are populated.  Fills in
/// `bvhNodes`, `triIndices`, `boundsMin`, and `boundsMax`.
void buildTriMeshBVH(WorldTriMesh& mesh);

/// @brief Sweep an AABB against a triangle mesh using BVH-accelerated SAT tests.
HitResult sweepAABBvsTriMesh(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldTriMesh& mesh);

/// @brief Push an AABB out of a triangle mesh using per-triangle SAT MTV.
///
/// For each triangle the AABB overlaps (BVH-accelerated leaf search), computes
/// the minimum-translation-vector that separates the AABB from the triangle and
/// applies it.  More accurate than depenetrating against leaf-AABB proxies:
/// curved surfaces (cylinders, spheres) feel curved instead of cubical.
///
/// Velocity is updated to cancel the component flowing into the surface, so the
/// entity slides along the contact rather than re-penetrating next frame.
///
/// Trade-off: at sharp triangle edges where adjacent face normals fight, the
/// per-triangle pushes can briefly disagree.  In practice this is rare and the
/// pushback bias keeps the entity off the surface.
///
/// @param pos          AABB centre — modified in place to push out of overlaps.
/// @param vel          Velocity — modified in place to cancel inward motion.
/// @param halfExtents  AABB half-extents.
/// @param mesh         Triangle mesh to depenetrate from.
/// @param pushback     Tiny extra distance added to each push to avoid hairline
///                     contact (matches Quake's DIST_EPSILON of 1/32 unit).
void depenetrateAABBvsTriMesh(
    glm::vec3& pos, glm::vec3& vel, glm::vec3 halfExtents, const WorldTriMesh& mesh, float pushback = 0.03125f);

} // namespace physics
