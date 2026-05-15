/// @file DebugCollisionDraw.hpp
/// @brief Debug accumulator for physics contact points and normals.
///
/// Physics code (CollisionSystem, SweptCollision, TriMeshCollision) pushes
/// contacts here at every hit event.  When recording is disabled (the default)
/// every push is wait-free and amounts to a single relaxed atomic load + branch
/// — safe to leave wired in the release path.
///
/// All writes go through a thread-local buffer so the multithreaded physics
/// kernels (e.g. `CollisionSystem`'s parallelFor over players) never contend.
/// Reads are made consistent by calling `beginFrame()` exactly once per
/// rendered frame from the main thread, BEFORE the next physics step starts.
///
/// **Determinism note.** The buffer is purely diagnostic — no entry ever feeds
/// back into the simulation.  The enable/disable atomic is `relaxed` because
/// out-of-order observation between threads cannot affect sim outcomes.

#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <span>

namespace physics::debug
{

/// @brief Source of a recorded contact, for filter / colour in the overlay.
enum class ContactSource : uint8_t
{
    PlaneSweep = 0,    ///< Swept-AABB hit on an infinite plane.
    BoxSweep = 1,      ///< Swept-AABB hit on a static AABB.
    BrushSweep = 2,    ///< Swept-AABB hit on a convex brush.
    CylinderSweep = 3, ///< Swept-AABB hit on a vertical cylinder.
    SphereSweep = 4,   ///< Swept-AABB hit on a sphere.
    TriMeshSweep = 5,  ///< Swept-AABB hit on a triangle (mesh).
    PlaneDepen = 6,    ///< Depenetration push out of plane overlap.
    BoxDepen = 7,      ///< Depenetration push out of static AABB.
    BrushDepen = 8,    ///< Depenetration push out of brush.
    CylinderDepen = 9, ///< Depenetration push out of cylinder.
    SphereDepen = 10,  ///< Depenetration push out of sphere.
    TriMeshDepen = 11, ///< Depenetration push from a triangle MTV contribution.
    Count = 12,
};

/// @brief A single recorded contact event.
///
/// `normal` always points from solid into free space (the direction the
/// entity is pushed).  `depth` is the depenetration depth in world units
/// (0 for swept hits, which have no penetration depth).
struct Contact
{
    glm::vec3 point{0.0f};       ///< World-space contact point.
    glm::vec3 normal{0.0f};      ///< Unit surface normal pointing into free space.
    float depth = 0.0f;          ///< Depenetration depth (0 for sweep hits).
    ContactSource source{};      ///< Origin classification (for filtering/colouring).
    uint32_t primitiveIndex = 0; ///< Source-primitive index (e.g. triangle id, box id).
};

/// @brief Push a contact event.  No-op when `isEnabled()` is false.
/// Thread-safe: each thread accumulates into its own thread-local buffer.
void pushContact(const Contact& c) noexcept;

/// @brief Convenience overload for non-depenetration (sweep) contacts.
void pushSweepContact(glm::vec3 point, glm::vec3 normal, ContactSource source, uint32_t primitiveIndex = 0) noexcept;

/// @brief Convenience overload for depenetration contacts (with depth).
void pushDepenContact(
    glm::vec3 point, glm::vec3 normal, float depth, ContactSource source, uint32_t primitiveIndex = 0) noexcept;

/// @brief Enable / disable recording.  When disabled, `pushContact` is a
/// single relaxed atomic load + branch.  Idempotent.
void setEnabled(bool on) noexcept;

/// @brief Query the recording flag.  Internal fast-path predicate.
[[nodiscard]] bool isEnabled() noexcept;

/// @brief Drain all thread-local buffers into the front buffer and clear them.
/// MUST be called exactly once per rendered frame from the main thread,
/// BEFORE the next physics step starts.  Calling concurrently with
/// `pushContact` is UB.
void beginFrame() noexcept;

/// @brief Snapshot of all contacts pushed since the last `beginFrame()`.
/// Stable until the next `beginFrame()`.  Read-only.
[[nodiscard]] std::span<const Contact> contacts() noexcept;

} // namespace physics::debug
