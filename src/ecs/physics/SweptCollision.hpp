#pragma once

#include <glm/vec3.hpp>
#include <span>

// Pure swept-collision math — no ECS types, no registry.
//
// Convention: a Plane divides space into free (dot(normal, p) > distance)
// and solid (dot(normal, p) < distance). The normal always points into free space.
//
// Example planes (Y-up):
//   Floor at y=0:          { normal=(0,1,0),  distance=0   }
//   Ceiling at y=512:      { normal=(0,-1,0), distance=-512 }
//   Wall at x=256 (solid right): { normal=(-1,0,0), distance=-256 }
namespace physics
{

struct Plane
{
    glm::vec3 normal; // unit vector pointing into free space
    float distance;   // dot(normal, p) == distance for points on the plane
};

struct HitResult
{
    bool hit{false};
    float tFirst{1.0f};                 // fraction along movement path [0..1]
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; // surface normal at the contact point
};

// Sweep an AABB along the path [start, end] against a list of infinite planes.
//
// Uses the Minkowski-sum approach: each plane is expanded outward by the AABB
// half-extents, reducing the problem to a ray-vs-expanded-plane intersection.
//
// Returns the earliest hit within the sweep, or HitResult{hit=false} if clear.
// Entities that start already inside a plane are skipped (depenetration is
// handled separately by CollisionSystem).
HitResult sweepAABB(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, std::span<const Plane> planes);

} // namespace physics
