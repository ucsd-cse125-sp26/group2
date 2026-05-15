/// @file BroadphaseTree.cpp
/// @brief Implementation of the Box2D-style dynamic AABB tree.

#include "ecs/physics/BroadphaseTree.hpp"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>

namespace physics::broadphase
{

namespace
{

constexpr int32_t k_invalid = -1;
constexpr int k_freeHeight = -1;

} // namespace

WorldAABB Tree::combine(const WorldAABB& a, const WorldAABB& b)
{
    return WorldAABB{
        .min = glm::min(a.min, b.min),
        .max = glm::max(a.max, b.max),
    };
}

float Tree::aabbArea(const WorldAABB& a)
{
    const glm::vec3 d = a.max - a.min;
    return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}

bool Tree::aabbOverlap(const WorldAABB& a, const WorldAABB& b)
{
    return a.max.x >= b.min.x && a.min.x <= b.max.x && a.max.y >= b.min.y && a.min.y <= b.max.y &&
           a.max.z >= b.min.z && a.min.z <= b.max.z;
}

bool Tree::aabbContains(const WorldAABB& container, const WorldAABB& inside)
{
    return container.min.x <= inside.min.x && container.min.y <= inside.min.y && container.min.z <= inside.min.z &&
           container.max.x >= inside.max.x && container.max.y >= inside.max.y && container.max.z >= inside.max.z;
}

int32_t Tree::allocNode()
{
    if (freeList_ != k_invalid) {
        const int32_t id = freeList_;
        freeList_ = nodes_[static_cast<size_t>(id)].parent; // reuse parent as free list link
        Node& n = nodes_[static_cast<size_t>(id)];
        n = Node{};
        return id;
    }
    nodes_.emplace_back();
    return static_cast<int32_t>(nodes_.size()) - 1;
}

void Tree::freeNode(int32_t id)
{
    Node& n = nodes_[static_cast<size_t>(id)];
    n.parent = freeList_;
    n.height = k_freeHeight;
    freeList_ = id;
}

Proxy Tree::insertProxy(const WorldAABB& aabb, entt::entity entity)
{
    const int32_t id = allocNode();
    Node& n = nodes_[static_cast<size_t>(id)];

    // Fatten by margin so small motions don't trigger re-insertion.
    n.aabb.min = aabb.min - glm::vec3{k_fatAabbMargin};
    n.aabb.max = aabb.max + glm::vec3{k_fatAabbMargin};
    n.entity = entity;
    n.height = 0;

    insertLeaf(id);
    ++proxyCount_;
    return Proxy{.id = id};
}

bool Tree::updateProxy(Proxy p, const WorldAABB& newAabb, glm::vec3 displacement)
{
    if (!p.valid())
        return false;
    Node& n = nodes_[static_cast<size_t>(p.id)];

    // Fatten new AABB by margin, extended along motion vector.
    const glm::vec3 d = k_motionPredict * displacement;
    WorldAABB fat;
    fat.min = newAabb.min - glm::vec3{k_fatAabbMargin};
    fat.max = newAabb.max + glm::vec3{k_fatAabbMargin};
    fat.min += glm::min(d, glm::vec3{0.0f});
    fat.max += glm::max(d, glm::vec3{0.0f});

    // If the new fat AABB is still inside the leaf's existing fat AABB, no
    // structural change is needed.
    if (aabbContains(n.aabb, fat))
        return false;

    // Otherwise, remove + reinsert.  This preserves the proxy id.
    removeLeaf(p.id);
    n.aabb = fat;
    insertLeaf(p.id);
    return true;
}

void Tree::removeProxy(Proxy p)
{
    if (!p.valid())
        return;
    removeLeaf(p.id);
    freeNode(p.id);
    --proxyCount_;
}

void Tree::insertLeaf(int32_t leaf)
{
    if (root_ == k_invalid) {
        root_ = leaf;
        nodes_[static_cast<size_t>(leaf)].parent = k_invalid;
        return;
    }

    // Stage 1 — find the best sibling using the surface-area heuristic.
    // Descend from the root, picking the child whose union with our leaf
    // has the smallest surface area.
    const WorldAABB leafAabb = nodes_[static_cast<size_t>(leaf)].aabb;
    int32_t index = root_;
    while (!isLeaf(index)) {
        const int32_t l = nodes_[static_cast<size_t>(index)].left;
        const int32_t r = nodes_[static_cast<size_t>(index)].right;
        const float area = aabbArea(nodes_[static_cast<size_t>(index)].aabb);

        const WorldAABB combined = combine(nodes_[static_cast<size_t>(index)].aabb, leafAabb);
        const float combinedArea = aabbArea(combined);

        // Cost of creating a new parent for this node and the new leaf.
        const float cost = 2.0f * combinedArea;
        // Minimum cost to push the leaf into a child.
        const float inheritanceCost = 2.0f * (combinedArea - area);

        auto childCost = [&](int32_t child) -> float {
            const WorldAABB childAabb = nodes_[static_cast<size_t>(child)].aabb;
            const float c = isLeaf(child) ? aabbArea(combine(leafAabb, childAabb))
                                          : aabbArea(combine(leafAabb, childAabb)) -
                                                aabbArea(childAabb);
            return c + inheritanceCost;
        };

        const float costL = childCost(l);
        const float costR = childCost(r);

        if (cost < costL && cost < costR)
            break;
        index = costL < costR ? l : r;
    }

    // Stage 2 — create a new parent above `index` and attach `leaf`.
    const int32_t sibling = index;
    const int32_t oldParent = nodes_[static_cast<size_t>(sibling)].parent;
    const int32_t newParent = allocNode();
    Node& np = nodes_[static_cast<size_t>(newParent)];
    np.parent = oldParent;
    np.aabb = combine(leafAabb, nodes_[static_cast<size_t>(sibling)].aabb);
    np.height = nodes_[static_cast<size_t>(sibling)].height + 1;

    if (oldParent != k_invalid) {
        Node& op = nodes_[static_cast<size_t>(oldParent)];
        if (op.left == sibling)
            op.left = newParent;
        else
            op.right = newParent;
    } else {
        root_ = newParent;
    }

    np.left = sibling;
    np.right = leaf;
    nodes_[static_cast<size_t>(sibling)].parent = newParent;
    nodes_[static_cast<size_t>(leaf)].parent = newParent;

    // Stage 3 — walk back up the tree, refit AABBs, and re-balance.
    int32_t up = nodes_[static_cast<size_t>(leaf)].parent;
    while (up != k_invalid) {
        up = balance(up);
        Node& n = nodes_[static_cast<size_t>(up)];
        const int32_t l = n.left;
        const int32_t r = n.right;
        n.height = 1 + std::max(nodes_[static_cast<size_t>(l)].height, nodes_[static_cast<size_t>(r)].height);
        n.aabb = combine(nodes_[static_cast<size_t>(l)].aabb, nodes_[static_cast<size_t>(r)].aabb);
        up = n.parent;
    }
}

void Tree::removeLeaf(int32_t leaf)
{
    if (leaf == root_) {
        root_ = k_invalid;
        return;
    }

    const int32_t parent = nodes_[static_cast<size_t>(leaf)].parent;
    const int32_t grandParent = nodes_[static_cast<size_t>(parent)].parent;
    const int32_t sibling = (nodes_[static_cast<size_t>(parent)].left == leaf)
                                ? nodes_[static_cast<size_t>(parent)].right
                                : nodes_[static_cast<size_t>(parent)].left;

    if (grandParent != k_invalid) {
        Node& gp = nodes_[static_cast<size_t>(grandParent)];
        if (gp.left == parent)
            gp.left = sibling;
        else
            gp.right = sibling;
        nodes_[static_cast<size_t>(sibling)].parent = grandParent;
        freeNode(parent);

        // Refit grand-parent and above.
        int32_t up = grandParent;
        while (up != k_invalid) {
            up = balance(up);
            Node& n = nodes_[static_cast<size_t>(up)];
            const int32_t l = n.left;
            const int32_t r = n.right;
            n.aabb = combine(nodes_[static_cast<size_t>(l)].aabb, nodes_[static_cast<size_t>(r)].aabb);
            n.height = 1 + std::max(nodes_[static_cast<size_t>(l)].height, nodes_[static_cast<size_t>(r)].height);
            up = n.parent;
        }
    } else {
        root_ = sibling;
        nodes_[static_cast<size_t>(sibling)].parent = k_invalid;
        freeNode(parent);
    }
}

int32_t Tree::balance(int32_t a)
{
    Node& A = nodes_[static_cast<size_t>(a)];
    if (isLeaf(a) || A.height < 2)
        return a;

    const int32_t b = A.left;
    const int32_t c = A.right;
    const int balance = nodes_[static_cast<size_t>(c)].height - nodes_[static_cast<size_t>(b)].height;

    // Rotate up if too heavy.
    if (balance > 1) {
        const int32_t f = nodes_[static_cast<size_t>(c)].left;
        const int32_t g = nodes_[static_cast<size_t>(c)].right;
        Node& C = nodes_[static_cast<size_t>(c)];

        C.left = a;
        C.parent = A.parent;
        A.parent = c;
        if (C.parent != k_invalid) {
            Node& cp = nodes_[static_cast<size_t>(C.parent)];
            if (cp.left == a)
                cp.left = c;
            else
                cp.right = c;
        } else {
            root_ = c;
        }

        if (nodes_[static_cast<size_t>(f)].height > nodes_[static_cast<size_t>(g)].height) {
            C.right = f;
            A.right = g;
            nodes_[static_cast<size_t>(g)].parent = a;
            A.aabb = combine(nodes_[static_cast<size_t>(b)].aabb, nodes_[static_cast<size_t>(g)].aabb);
            C.aabb = combine(A.aabb, nodes_[static_cast<size_t>(f)].aabb);
            A.height = 1 + std::max(nodes_[static_cast<size_t>(b)].height, nodes_[static_cast<size_t>(g)].height);
            C.height = 1 + std::max(A.height, nodes_[static_cast<size_t>(f)].height);
        } else {
            C.right = g;
            A.right = f;
            nodes_[static_cast<size_t>(f)].parent = a;
            A.aabb = combine(nodes_[static_cast<size_t>(b)].aabb, nodes_[static_cast<size_t>(f)].aabb);
            C.aabb = combine(A.aabb, nodes_[static_cast<size_t>(g)].aabb);
            A.height = 1 + std::max(nodes_[static_cast<size_t>(b)].height, nodes_[static_cast<size_t>(f)].height);
            C.height = 1 + std::max(A.height, nodes_[static_cast<size_t>(g)].height);
        }
        return c;
    }

    if (balance < -1) {
        const int32_t d = nodes_[static_cast<size_t>(b)].left;
        const int32_t e = nodes_[static_cast<size_t>(b)].right;
        Node& B = nodes_[static_cast<size_t>(b)];

        B.left = a;
        B.parent = A.parent;
        A.parent = b;
        if (B.parent != k_invalid) {
            Node& bp = nodes_[static_cast<size_t>(B.parent)];
            if (bp.left == a)
                bp.left = b;
            else
                bp.right = b;
        } else {
            root_ = b;
        }

        if (nodes_[static_cast<size_t>(d)].height > nodes_[static_cast<size_t>(e)].height) {
            B.right = d;
            A.left = e;
            nodes_[static_cast<size_t>(e)].parent = a;
            A.aabb = combine(nodes_[static_cast<size_t>(c)].aabb, nodes_[static_cast<size_t>(e)].aabb);
            B.aabb = combine(A.aabb, nodes_[static_cast<size_t>(d)].aabb);
            A.height = 1 + std::max(nodes_[static_cast<size_t>(c)].height, nodes_[static_cast<size_t>(e)].height);
            B.height = 1 + std::max(A.height, nodes_[static_cast<size_t>(d)].height);
        } else {
            B.right = e;
            A.left = d;
            nodes_[static_cast<size_t>(d)].parent = a;
            A.aabb = combine(nodes_[static_cast<size_t>(c)].aabb, nodes_[static_cast<size_t>(d)].aabb);
            B.aabb = combine(A.aabb, nodes_[static_cast<size_t>(e)].aabb);
            A.height = 1 + std::max(nodes_[static_cast<size_t>(c)].height, nodes_[static_cast<size_t>(d)].height);
            B.height = 1 + std::max(A.height, nodes_[static_cast<size_t>(e)].height);
        }
        return b;
    }

    return a;
}

void Tree::queryAABB(const WorldAABB& aabb, const std::function<bool(entt::entity)>& visit) const
{
    if (root_ == k_invalid)
        return;

    std::vector<int32_t> stack;
    stack.push_back(root_);
    while (!stack.empty()) {
        const int32_t id = stack.back();
        stack.pop_back();
        const Node& n = nodes_[static_cast<size_t>(id)];
        if (!aabbOverlap(n.aabb, aabb))
            continue;
        if (n.left == k_invalid && n.right == k_invalid) {
            // Leaf
            if (!visit(n.entity))
                return;
            continue;
        }
        if (n.left != k_invalid)
            stack.push_back(n.left);
        if (n.right != k_invalid)
            stack.push_back(n.right);
    }
}

void Tree::raycast(glm::vec3 origin, glm::vec3 dir, float maxT, const std::function<bool(entt::entity)>& visit) const
{
    if (root_ == k_invalid)
        return;

    // Slab ray-AABB at each node.
    auto rayHitsAabb = [origin, dir, maxT](const WorldAABB& b) -> bool {
        float tEntry = 0.0f;
        float tExit = maxT;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(dir[axis]) < 1e-8f) {
                if (origin[axis] < b.min[axis] || origin[axis] > b.max[axis])
                    return false;
                continue;
            }
            const float inv = 1.0f / dir[axis];
            float t1 = (b.min[axis] - origin[axis]) * inv;
            float t2 = (b.max[axis] - origin[axis]) * inv;
            if (t1 > t2)
                std::swap(t1, t2);
            tEntry = std::max(tEntry, t1);
            tExit = std::min(tExit, t2);
            if (tEntry > tExit)
                return false;
        }
        return true;
    };

    std::vector<int32_t> stack;
    stack.push_back(root_);
    while (!stack.empty()) {
        const int32_t id = stack.back();
        stack.pop_back();
        const Node& n = nodes_[static_cast<size_t>(id)];
        if (!rayHitsAabb(n.aabb))
            continue;
        if (n.left == k_invalid && n.right == k_invalid) {
            if (!visit(n.entity))
                return;
            continue;
        }
        if (n.left != k_invalid)
            stack.push_back(n.left);
        if (n.right != k_invalid)
            stack.push_back(n.right);
    }
}

void Tree::sweptAABB(glm::vec3 halfExtents,
                     glm::vec3 start,
                     glm::vec3 end,
                     const std::function<bool(entt::entity)>& visit) const
{
    // Build a single AABB covering the entire swept volume.  Conservative
    // but cheap; refine to a slab test against the swept-box per node if
    // necessary.
    WorldAABB sweepBounds;
    sweepBounds.min = glm::min(start, end) - halfExtents;
    sweepBounds.max = glm::max(start, end) + halfExtents;
    queryAABB(sweepBounds, visit);
}

void Tree::clear()
{
    nodes_.clear();
    root_ = k_invalid;
    freeList_ = k_invalid;
    proxyCount_ = 0;
}

WorldAABB Tree::nodeAabb(int32_t nodeId) const noexcept
{
    if (nodeId < 0 || nodeId >= static_cast<int32_t>(nodes_.size()))
        return WorldAABB{glm::vec3{0.0f}, glm::vec3{0.0f}};
    return nodes_[static_cast<size_t>(nodeId)].aabb;
}

bool Tree::isLeaf(int32_t nodeId) const noexcept
{
    if (nodeId < 0 || nodeId >= static_cast<int32_t>(nodes_.size()))
        return false;
    return nodes_[static_cast<size_t>(nodeId)].height == 0;
}

int32_t Tree::leftChild(int32_t nodeId) const noexcept
{
    if (nodeId < 0 || nodeId >= static_cast<int32_t>(nodes_.size()))
        return k_invalid;
    return nodes_[static_cast<size_t>(nodeId)].left;
}

int32_t Tree::rightChild(int32_t nodeId) const noexcept
{
    if (nodeId < 0 || nodeId >= static_cast<int32_t>(nodes_.size()))
        return k_invalid;
    return nodes_[static_cast<size_t>(nodeId)].right;
}

} // namespace physics::broadphase
