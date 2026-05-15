/// @file BroadphaseTree.hpp
/// @brief Dynamic AABB tree for O(log n) overlap queries.
///
/// Box2D-style `b2DynamicTree`: a binary tree of "fat" AABBs (padded by a
/// margin so small movements don't require re-insertion).  Each leaf
/// stores a stable `Proxy` handle plus user data (an `entt::entity`).
/// Internal nodes track the union AABB of their children.
///
/// **Operations.**
///   - `insertProxy(aabb, entity)` → returns a `Proxy` handle.
///   - `updateProxy(p, newAabb)` → re-positions if it left its fat AABB.
///   - `removeProxy(p)` → destroys the leaf.
///   - `queryAABB(aabb, visit)` → calls `visit(entity)` for every
///       overlapping leaf.
///   - `raycast(origin, dir, maxT, visit)` → calls `visit` for every leaf
///       the ray segment intersects (Slab test).
///
/// **Determinism.**  Tree topology depends on insertion order; for
/// reproducible behaviour, callers insert proxies in stable entity-id
/// order (see `Phase 15` audit checklist).
///
/// **Memory.** Nodes are stored in a pool indexed by 32-bit ids; the free
/// list reuses slots so capacity stays bounded.

#pragma once

#include "ecs/physics/SweptCollision.hpp"

#include <cstdint>
#include <entt/entt.hpp>
#include <functional>
#include <glm/vec3.hpp>
#include <vector>

namespace physics::broadphase
{

/// @brief Stable handle to a tree leaf.  Survives across insertions and
/// removals of other proxies as long as the proxy itself is alive.
struct Proxy
{
    int32_t id = -1;
    [[nodiscard]] bool valid() const noexcept { return id >= 0; }
};

/// @brief Dynamic AABB tree.  Not thread-safe — every mutating call must
/// be serialised by the caller (typically the physics thread).
class Tree
{
public:
    /// @brief Margin added to every leaf's AABB so it doesn't reinsert on
    /// tiny movements.  In world units (matches your Quake-unit scale).
    static constexpr float k_fatAabbMargin = 4.0f;

    /// @brief How aggressively to extend the fat AABB along the body's
    /// motion vector each update — multiplied by the displacement.
    static constexpr float k_motionPredict = 2.0f;

    Tree() = default;

    /// @brief Insert a leaf at the given AABB.  Returns a handle the caller
    /// must keep for later update / remove.
    Proxy insertProxy(const WorldAABB& aabb, entt::entity entity);

    /// @brief Notify the tree that the entity's AABB moved.  Returns true
    /// if the tree topology was actually adjusted.
    /// @param p          Proxy from a prior `insertProxy`.
    /// @param newAabb    Tight AABB of the entity at its new position.
    /// @param displacement  Movement vector; used to fatten the new AABB
    ///                   so the next few frames stay inside the same leaf.
    bool updateProxy(Proxy p, const WorldAABB& newAabb, glm::vec3 displacement);

    /// @brief Destroy the leaf.  The `Proxy` handle becomes invalid.
    void removeProxy(Proxy p);

    /// @brief Walk every leaf whose fat AABB overlaps `aabb`, calling
    /// `visit(entity)` for each.  The visitor returns `false` to stop
    /// early, `true` to continue.
    void queryAABB(const WorldAABB& aabb, const std::function<bool(entt::entity)>& visit) const;

    /// @brief Walk every leaf whose AABB intersects the ray segment
    /// `[origin, origin + dir * maxT]`.  Visitor returns `false` to stop.
    void raycast(glm::vec3 origin, glm::vec3 dir, float maxT, const std::function<bool(entt::entity)>& visit) const;

    /// @brief Walk every leaf whose AABB intersects the swept-AABB
    /// `[start, end]` expanded by `halfExtents`.  Used for player /
    /// projectile sweeps against dynamic bodies.
    void sweptAABB(glm::vec3 halfExtents,
                   glm::vec3 start,
                   glm::vec3 end,
                   const std::function<bool(entt::entity)>& visit) const;

    /// @brief Clear all proxies and release pool memory.
    void clear();

    /// @brief Current proxy count (live leaves).
    [[nodiscard]] int proxyCount() const noexcept { return proxyCount_; }

    /// @brief Root node id (or -1 if empty).  Exposed for debug viz.
    [[nodiscard]] int32_t rootIndex() const noexcept { return root_; }

    /// @brief Read-only access to a node's AABB.  Returns `(boundsMin == boundsMax == 0)`
    /// for an invalid id.
    [[nodiscard]] WorldAABB nodeAabb(int32_t nodeId) const noexcept;

    /// @brief True iff the node is a leaf (no children, has entity).
    [[nodiscard]] bool isLeaf(int32_t nodeId) const noexcept;

    /// @brief Left/right child of an interior node (or -1 if leaf / invalid).
    [[nodiscard]] int32_t leftChild(int32_t nodeId) const noexcept;
    [[nodiscard]] int32_t rightChild(int32_t nodeId) const noexcept;

private:
    struct Node
    {
        WorldAABB aabb{glm::vec3{0.0f}, glm::vec3{0.0f}};
        entt::entity entity{entt::null}; ///< Leaf only.
        int32_t parent = -1;
        int32_t left = -1;
        int32_t right = -1;
        int32_t height = 0; ///< 0 = leaf; -1 = free.
    };

    std::vector<Node> nodes_;
    int32_t root_ = -1;
    int32_t freeList_ = -1;
    int proxyCount_ = 0;

    int32_t allocNode();
    void freeNode(int32_t id);
    void insertLeaf(int32_t leaf);
    void removeLeaf(int32_t leaf);
    static WorldAABB combine(const WorldAABB& a, const WorldAABB& b);
    static float aabbArea(const WorldAABB& a);
    static bool aabbOverlap(const WorldAABB& a, const WorldAABB& b);
    static bool aabbContains(const WorldAABB& container, const WorldAABB& inside);
    int32_t balance(int32_t a);
};

} // namespace physics::broadphase
