/// @file ContactManifold.hpp
/// @brief Multi-point contact manifold + persistent contact cache.
///
/// Up to 4 contact points per body pair, stable across frames so the
/// sequential-impulse solver in Phase 10 can warm-start its impulses
/// from last frame's converged solution.
///
/// **Data layout** mirrors Box2D / Bullet:
///   - One world-space contact normal (points from A → B).
///   - Per-point world position, depth, and accumulated normal /
///     tangent impulses (carried across frames for warm-start).
///   - A "feature id" per point so we can match this frame's points to
///     last frame's even when contact geometry slides slightly.

#pragma once

#include "ecs/physics/SurfaceType.hpp"

#include <array>
#include <cstdint>
#include <entt/entt.hpp>
#include <glm/vec3.hpp>

namespace physics
{

/// @brief Up to 4 points per body pair — enough for any flat-face contact
/// (box-on-box, box-on-ground, etc.).  Curved-on-curved (sphere-sphere)
/// uses 1 point.  Capsule-on-flat uses 1–2 points.
inline constexpr int k_maxContactPoints = 4;

/// @brief Identifier built from "which feature of A" and "which feature of
/// B" met at this contact.  Used for matching points across frames.
/// 0xFFFFFFFF = uncomputed / synthetic single-point contact.
struct ContactFeatureId
{
    uint32_t value = 0xFFFFFFFFu;

    [[nodiscard]] bool operator==(const ContactFeatureId& other) const noexcept { return value == other.value; }
};

/// @brief One sub-contact in a multi-point manifold.
struct ContactPoint
{
    glm::vec3 worldPositionA{0.0f}; ///< World-space hit point on body A.
    glm::vec3 worldPositionB{0.0f}; ///< World-space hit point on body B.
    glm::vec3 localA{0.0f};         ///< Body-A-local offset for warm-start matching.
    glm::vec3 localB{0.0f};         ///< Body-B-local offset.
    float depth = 0.0f;             ///< Penetration depth (positive = overlapping).
    float normalImpulse = 0.0f;     ///< Accumulated normal impulse, carried across frames.
    float tangentImpulse[2] = {0.0f, 0.0f}; ///< Two friction tangents.
    ContactFeatureId featureId{};
};

/// @brief A contact manifold between exactly two entities.  Contains 1–4
/// contact points sharing a single normal.
struct ContactManifold
{
    entt::entity a{entt::null};
    entt::entity b{entt::null};
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; ///< Points from A → B (out of A's surface).
    int pointCount = 0;
    std::array<ContactPoint, k_maxContactPoints> points{};
    SurfaceType surfaceA = SurfaceType::Concrete;
    SurfaceType surfaceB = SurfaceType::Concrete;

    /// @brief Static / kinematic flag.  When true, body A is treated as
    /// infinite mass during impulse application — typical for the world.
    bool aIsStatic = false;
    bool bIsStatic = false;
};

} // namespace physics
