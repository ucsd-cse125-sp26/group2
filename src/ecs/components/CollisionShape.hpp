/// @file CollisionShape.hpp
/// @brief Collision shape component — AABB or vertical capsule.

#pragma once

#include <cmath>
#include <cstdint>
#include <glm/vec3.hpp>

/// @brief Which shape kind the entity uses for collision queries.
enum class CollisionShapeType : uint8_t
{
    AABB = 0,    ///< Axis-aligned box; uses `halfExtents`.
    Capsule = 1, ///< Vertical capsule (axis = +Y); uses `radius` + `halfHeight`.
};

/// @brief Collision shape attached to an entity.
///
/// The shape is positioned at the entity's `Position` component.  For an
/// AABB the full extent spans `[pos - halfExtents, pos + halfExtents]`.
/// For a capsule, the segment endpoints are `pos ± (0, halfHeight, 0)`
/// and the radius `r` wraps around the segment.
///
/// Default is a standing-player AABB in Quake-ish units:
///   - Width  = 32  (`halfExtents.x/z = 16`)
///   - Height = 72  (`halfExtents.y   = 36`)
/// Switch the player to capsule at creation time by setting:
///   - `type = CollisionShapeType::Capsule`
///   - `radius = 16` (cylinder cross-section)
///   - `halfHeight = 20` (segment ends 20 u above/below centre)
///   - `halfExtents = (16, 36, 16)` (conservative bounding-box used by
///     axis-aligned swept queries — exact for axis-aligned face normals)
struct CollisionShape
{
    /// @brief Which kind of shape this is — selects the runtime collision path.
    CollisionShapeType type = CollisionShapeType::AABB;

    /// @brief AABB half-dimensions.  Used directly when `type == AABB`, and as
    /// the conservative bounding-box for the capsule when `type == Capsule`
    /// (matches the capsule's swept-AABB Minkowski hull).
    glm::vec3 halfExtents{16.0f, 36.0f, 16.0f};

    /// @brief Capsule cylinder radius.  Unused when `type == AABB`.
    float radius = 16.0f;

    /// @brief Capsule cylinder half-height (excludes spherical caps).
    /// Total visual height = 2 * (halfHeight + radius).  Unused when `type == AABB`.
    float halfHeight = 20.0f;

    /// @brief Minkowski extent of the shape along a unit direction `n`.
    /// Used by every Minkowski-sum-based collision query (plane sweep,
    /// box slab, triangle plane).  Returns the half-width of the shape's
    /// projection on `n`.
    [[nodiscard]] float minkowskiExtent(const glm::vec3& n) const noexcept
    {
        if (type == CollisionShapeType::Capsule) {
            // Capsule projects to a line segment of length 2*halfHeight*|n.y|
            // plus the radius on each end.
            return radius + halfHeight * std::abs(n.y);
        }
        // AABB:
        return std::abs(n.x) * halfExtents.x + std::abs(n.y) * halfExtents.y + std::abs(n.z) * halfExtents.z;
    }
};
