/// @file CollisionShape.hpp
/// @brief AABB collision shape component for physics entities.

#pragma once

#include <glm/vec3.hpp>

/// @brief Axis-aligned bounding box defined by half-extents from the entity's Position.
///
/// The full bounding box spans `[pos - halfExtents, pos + halfExtents]`.
///
/// Default is a standing player in Quake-ish units:
/// - Width  = 32  (`halfExtents.x/z = 16`)
/// - Height = 72  (`halfExtents.y   = 36`)
struct CollisionShape
{
    glm::vec3 halfExtents{16.0f, 36.0f, 16.0f}; ///< AABB half-dimensions (units).
};
