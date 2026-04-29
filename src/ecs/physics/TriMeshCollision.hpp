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

} // namespace physics
