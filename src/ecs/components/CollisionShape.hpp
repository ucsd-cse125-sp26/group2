#pragma once

#include <glm/vec3.hpp>

// Axis-aligned bounding box defined by half-extents from the entity's Position.
// The full bbox spans [pos - halfExtents, pos + halfExtents].
//
// Default is a standing player in Quake-ish units:
//   width  = 32  (halfExtents.x/z = 16)
//   height = 72  (halfExtents.y   = 36)
struct CollisionShape
{
    glm::vec3 halfExtents{16.0f, 36.0f, 16.0f};
};
