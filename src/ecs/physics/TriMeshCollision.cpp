/// @file TriMeshCollision.cpp
/// @brief BVH builder and swept-AABB-vs-triangle-mesh collision.

#include "TriMeshCollision.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace physics
{

namespace
{

constexpr int k_maxLeafTris = 4; ///< Max triangles per BVH leaf.

// BVH helpers

/// @brief Compute the AABB of a set of triangles referenced by triIndices[from..from+count).
void computeNodeBounds(const WorldTriMesh& mesh, int from, int count, glm::vec3& outMin, glm::vec3& outMax)
{
    outMin = glm::vec3(1e30f);
    outMax = glm::vec3(-1e30f);
    for (int i = from; i < from + count; ++i) {
        const uint32_t ti = mesh.triIndices[static_cast<size_t>(i)];
        for (int vi = 0; vi < 3; ++vi) {
            const glm::vec3& v = mesh.vertices[mesh.indices[ti * 3 + static_cast<uint32_t>(vi)]];
            outMin = glm::min(outMin, v);
            outMax = glm::max(outMax, v);
        }
    }
}

/// @brief Compute the centroid of triangle `ti` along `axis`.
float triCentroid(const WorldTriMesh& mesh, uint32_t ti, int axis)
{
    const glm::vec3& v0 = mesh.vertices[mesh.indices[ti * 3 + 0]];
    const glm::vec3& v1 = mesh.vertices[mesh.indices[ti * 3 + 1]];
    const glm::vec3& v2 = mesh.vertices[mesh.indices[ti * 3 + 2]];
    return (v0[axis] + v1[axis] + v2[axis]) / 3.0f;
}

/// @brief Recursively subdivide a BVH node.
void subdivide(WorldTriMesh& mesh, int nodeIdx)
{
    BVHNode& node = mesh.bvhNodes[static_cast<size_t>(nodeIdx)];
    if (node.count <= k_maxLeafTris)
        return; // leaf — small enough

    // Find longest axis.
    const glm::vec3 extent = node.boundsMax - node.boundsMin;
    int axis = 0;
    if (extent.y > extent[axis])
        axis = 1;
    if (extent.z > extent[axis])
        axis = 2;

    const float splitPos = (node.boundsMin[axis] + node.boundsMax[axis]) * 0.5f;

    // Partition triIndices: centroids < splitPos go left.
    const int from = node.leftFirst;
    const int to = from + node.count;
    int mid = from;
    for (int i = from; i < to; ++i) {
        if (triCentroid(mesh, mesh.triIndices[static_cast<size_t>(i)], axis) < splitPos) {
            std::swap(mesh.triIndices[static_cast<size_t>(i)], mesh.triIndices[static_cast<size_t>(mid)]);
            ++mid;
        }
    }

    // Degenerate split — force even split.
    const int leftCount = mid - from;
    if (leftCount == 0 || leftCount == node.count)
        mid = from + node.count / 2;

    const int rightFrom = mid;
    const int finalLeftCount = mid - from;
    const int rightCount = node.count - finalLeftCount;

    // Allocate child nodes.
    const int leftIdx = static_cast<int>(mesh.bvhNodes.size());
    mesh.bvhNodes.push_back(BVHNode{});
    mesh.bvhNodes.push_back(BVHNode{});

    // Left child.
    BVHNode& left = mesh.bvhNodes[static_cast<size_t>(leftIdx)];
    left.leftFirst = from;
    left.count = finalLeftCount;
    computeNodeBounds(mesh, from, finalLeftCount, left.boundsMin, left.boundsMax);

    // Right child.
    BVHNode& right = mesh.bvhNodes[static_cast<size_t>(leftIdx + 1)];
    right.leftFirst = rightFrom;
    right.count = rightCount;
    computeNodeBounds(mesh, rightFrom, rightCount, right.boundsMin, right.boundsMax);

    // Convert current node from leaf to interior.
    // IMPORTANT: re-fetch because push_back may have invalidated the reference.
    mesh.bvhNodes[static_cast<size_t>(nodeIdx)].leftFirst = leftIdx;
    mesh.bvhNodes[static_cast<size_t>(nodeIdx)].count = 0;

    subdivide(mesh, leftIdx);
    subdivide(mesh, leftIdx + 1);
}

// Swept-AABB vs BVH node AABB (quick overlap test)

/// @brief Test if a swept AABB overlaps a static AABB, considering only hits
///        closer than `tMax`.  Used for BVH node culling.
bool sweptAABBOverlapsAABB(
    glm::vec3 halfExtents, glm::vec3 start, glm::vec3 delta, glm::vec3 boxMin, glm::vec3 boxMax, float tMax)
{
    const glm::vec3 expMin = boxMin - halfExtents;
    const glm::vec3 expMax = boxMax + halfExtents;

    float tEntry = 0.0f;
    float tExit = tMax;

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(delta[axis]) < 1e-8f) {
            if (start[axis] < expMin[axis] || start[axis] > expMax[axis])
                return false;
        } else {
            const float invD = 1.0f / delta[axis];
            float t1 = (expMin[axis] - start[axis]) * invD;
            float t2 = (expMax[axis] - start[axis]) * invD;
            if (t1 > t2)
                std::swap(t1, t2);
            tEntry = std::max(tEntry, t1);
            tExit = std::min(tExit, t2);
            if (tEntry > tExit)
                return false;
        }
    }
    return true;
}

// Swept-AABB vs single triangle (plane test + SAT confirmation)

/// @brief Test if a static AABB (centered at `center`, half-extents `he`)
///        overlaps triangle (v0, v1, v2) using the separating axis theorem.
///        Tests 13 axes: 3 AABB face normals + 1 triangle normal + 9 edge crosses.
bool staticAABBvsTriSAT(glm::vec3 center, glm::vec3 he, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2)
{
    // Translate triangle so AABB center is at origin.
    const glm::vec3 a = v0 - center;
    const glm::vec3 b = v1 - center;
    const glm::vec3 c = v2 - center;

    const glm::vec3 edges[3] = {b - a, c - b, a - c};

    // 1. AABB face normals (3 axes: X, Y, Z) — project triangle onto each axis.
    for (int axis = 0; axis < 3; ++axis) {
        const float triMin = std::min({a[axis], b[axis], c[axis]});
        const float triMax = std::max({a[axis], b[axis], c[axis]});
        if (triMax < -he[axis] || triMin > he[axis])
            return false;
    }

    // 2. Triangle face normal.
    const glm::vec3 triN = glm::cross(edges[0], edges[1]);
    if (glm::length(triN) > 1e-8f) {
        const float r = he.x * std::abs(triN.x) + he.y * std::abs(triN.y) + he.z * std::abs(triN.z);
        const float s = glm::dot(triN, a); // distance from origin (AABB center) to tri plane
        if (std::abs(s) > r)
            return false;
    }

    // 3. Cross-product axes: AABB edge × triangle edge (9 axes).
    //    AABB edges are (1,0,0), (0,1,0), (0,0,1).
    const glm::vec3 aabbAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            const glm::vec3 ax = glm::cross(aabbAxes[i], edges[j]);
            if (glm::dot(ax, ax) < 1e-10f)
                continue; // parallel — skip

            const float r = he.x * std::abs(ax.x) + he.y * std::abs(ax.y) + he.z * std::abs(ax.z);
            const float p0 = glm::dot(ax, a);
            const float p1 = glm::dot(ax, b);
            const float p2 = glm::dot(ax, c);
            const float triMin = std::min({p0, p1, p2});
            const float triMax = std::max({p0, p1, p2});
            if (triMax < -r || triMin > r)
                return false;
        }
    }

    return true; // no separating axis found — overlapping
}

/// @brief Compute the minimum-translation-vector (MTV) that separates a static
///        AABB from a single triangle, using the same 13 SAT axes as
///        `staticAABBvsTriSAT`.  Returns true if they overlap; on overlap,
///        `outMtv` is the smallest vector that — when added to the AABB centre
///        — makes the shapes disjoint.
///
/// All cross-product axes are normalised before measuring overlap, so the
/// returned depth is in world units across every axis (the boolean SAT test
/// in `staticAABBvsTriSAT` skips this normalisation because it only needs to
/// compare signs).
bool aabbVsTriMTV(glm::vec3 center, glm::vec3 he, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, glm::vec3& outMtv)
{
    // Translate triangle so AABB centre is at origin (mirrors staticAABBvsTriSAT).
    const glm::vec3 a = v0 - center;
    const glm::vec3 b = v1 - center;
    const glm::vec3 c = v2 - center;

    const glm::vec3 edges[3] = {b - a, c - b, a - c};

    glm::vec3 bestAxis(0.0f);
    float bestDepth = 1e30f;

    // For one SAT axis: project AABB ([-r, r] in axis-local coords) and triangle
    // ([triMin, triMax]) and update the best-MTV running tally.  Returns false
    // if the axis is a separating axis (no overlap → no need to test others).
    auto considerAxis = [&](const glm::vec3& axis, float triMin, float triMax, float r) -> bool {
        if (triMax < -r || triMin > r)
            return false; // separating axis — shapes are disjoint

        // Two ways to push the AABB out of the overlap on this axis:
        //   push +axis by (triMax + r): AABB.min ends up at triMax (touching from -side)
        //   push -axis by (r - triMin): AABB.max ends up at triMin (touching from +side)
        // Pick whichever is smaller; that's the axis-local MTV contribution.
        const float pushPlus = triMax + r;
        const float pushMinus = r - triMin;

        const float depth = std::min(pushPlus, pushMinus);
        const glm::vec3 dir = (pushPlus < pushMinus) ? axis : -axis;

        if (depth < bestDepth) {
            bestDepth = depth;
            bestAxis = dir;
        }
        return true;
    };

    // 1. AABB face normals — already unit length.
    for (int axIdx = 0; axIdx < 3; ++axIdx) {
        glm::vec3 ax(0.0f);
        ax[axIdx] = 1.0f;
        const float triMin = std::min({a[axIdx], b[axIdx], c[axIdx]});
        const float triMax = std::max({a[axIdx], b[axIdx], c[axIdx]});
        if (!considerAxis(ax, triMin, triMax, he[axIdx]))
            return false;
    }

    // 2. Triangle face normal — normalise so depth is in world units.
    glm::vec3 triN = glm::cross(edges[0], edges[1]);
    const float triNLen = glm::length(triN);
    if (triNLen > 1e-8f) {
        triN /= triNLen;
        const float r = he.x * std::abs(triN.x) + he.y * std::abs(triN.y) + he.z * std::abs(triN.z);
        const float s = glm::dot(triN, a); // all 3 vertices are coplanar → single value
        if (!considerAxis(triN, s, s, r))
            return false;
    }

    // 3. Cross-product axes (3 AABB edges × 3 triangle edges = 9 axes).
    const glm::vec3 aabbAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            glm::vec3 ax = glm::cross(aabbAxes[i], edges[j]);
            const float axLen = glm::length(ax);
            if (axLen < 1e-5f)
                continue; // edges parallel — degenerate axis, skip
            ax /= axLen;

            const float r = he.x * std::abs(ax.x) + he.y * std::abs(ax.y) + he.z * std::abs(ax.z);
            const float p0 = glm::dot(ax, a);
            const float p1 = glm::dot(ax, b);
            const float p2 = glm::dot(ax, c);
            const float triMin = std::min({p0, p1, p2});
            const float triMax = std::max({p0, p1, p2});
            if (!considerAxis(ax, triMin, triMax, r))
                return false;
        }
    }

    outMtv = bestAxis * bestDepth;
    return true;
}

/// @brief Swept AABB vs a single triangle.
HitResult
sweepAABBvsTriangle(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2)
{
    HitResult result;

    // Triangle normal.
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    glm::vec3 triN = glm::cross(e1, e2);
    const float triNLen = glm::length(triN);
    if (triNLen < 1e-8f)
        return result; // degenerate triangle
    triN /= triNLen;

    // Ensure the normal faces toward the start position.
    if (glm::dot(triN, start - v0) < 0.0f)
        triN = -triN;

    // Minkowski expansion along triangle normal.
    const float r =
        std::abs(triN.x) * halfExtents.x + std::abs(triN.y) * halfExtents.y + std::abs(triN.z) * halfExtents.z;

    const float distStart = glm::dot(triN, start) - glm::dot(triN, v0);
    const float distEnd = glm::dot(triN, end) - glm::dot(triN, v0);

    // Must start outside (in front of) the expanded plane.
    if (distStart < r)
        return result;
    // Must be moving toward the plane.
    if (distEnd >= distStart)
        return result;

    const float t = (distStart - r) / (distStart - distEnd);
    if (t < 0.0f || t >= 1.0f)
        return result;

    // AABB center at contact time.
    const glm::vec3 contactPos = start + (end - start) * t;

    // SAT confirmation: does the AABB at contactPos actually overlap the triangle?
    if (!staticAABBvsTriSAT(contactPos, halfExtents, v0, v1, v2))
        return result;

    result.hit = true;
    result.tFirst = t;
    result.normal = triN;
    return result;
}

} // namespace

// Public API

void buildTriMeshBVH(WorldTriMesh& mesh)
{
    const uint32_t numTris = static_cast<uint32_t>(mesh.indices.size() / 3);
    if (numTris == 0)
        return;

    // Initialize triIndices as identity permutation [0, 1, 2, ...].
    mesh.triIndices.resize(numTris);
    for (uint32_t i = 0; i < numTris; ++i)
        mesh.triIndices[i] = i;

    // Compute overall bounds.
    mesh.boundsMin = glm::vec3(1e30f);
    mesh.boundsMax = glm::vec3(-1e30f);
    for (const auto& v : mesh.vertices) {
        mesh.boundsMin = glm::min(mesh.boundsMin, v);
        mesh.boundsMax = glm::max(mesh.boundsMax, v);
    }

    // Root node.
    mesh.bvhNodes.clear();
    mesh.bvhNodes.reserve(numTris * 2); // worst case
    mesh.bvhNodes.push_back(BVHNode{
        .boundsMin = mesh.boundsMin,
        .boundsMax = mesh.boundsMax,
        .leftFirst = 0,
        .count = static_cast<int>(numTris),
    });

    subdivide(mesh, 0);
}

HitResult sweepAABBvsTriMesh(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldTriMesh& mesh)
{
    HitResult best;
    if (mesh.bvhNodes.empty())
        return best;

    const glm::vec3 delta = end - start;

    // Quick reject against mesh AABB.
    if (!sweptAABBOverlapsAABB(halfExtents, start, delta, mesh.boundsMin, mesh.boundsMax, 1.0f))
        return best;

    // Iterative BVH traversal with fixed-size stack.
    int stack[64];
    int stackPtr = 0;
    stack[0] = 0; // root

    while (stackPtr >= 0) {
        const int nodeIdx = stack[stackPtr--];
        const BVHNode& node = mesh.bvhNodes[static_cast<size_t>(nodeIdx)];

        // Cull against this node's AABB using the best-so-far time.
        if (!sweptAABBOverlapsAABB(halfExtents, start, delta, node.boundsMin, node.boundsMax, best.tFirst))
            continue;

        if (node.count > 0) {
            // Leaf — test individual triangles.
            for (int i = node.leftFirst; i < node.leftFirst + node.count; ++i) {
                const uint32_t ti = mesh.triIndices[static_cast<size_t>(i)];
                const glm::vec3& v0 = mesh.vertices[mesh.indices[ti * 3 + 0]];
                const glm::vec3& v1 = mesh.vertices[mesh.indices[ti * 3 + 1]];
                const glm::vec3& v2 = mesh.vertices[mesh.indices[ti * 3 + 2]];

                const HitResult hr = sweepAABBvsTriangle(halfExtents, start, end, v0, v1, v2);
                if (hr.hit && hr.tFirst < best.tFirst)
                    best = hr;
            }
        } else {
            // Interior — push children.
            stack[++stackPtr] = node.leftFirst;
            stack[++stackPtr] = node.leftFirst + 1;
        }
    }

    return best;
}

void depenetrateAABBvsTriMesh(
    glm::vec3& pos, glm::vec3& vel, glm::vec3 halfExtents, const WorldTriMesh& mesh, float pushback)
{
    if (mesh.bvhNodes.empty())
        return;

    // Quick reject: AABB must overlap the whole-mesh bounds (Minkowski-expanded).
    if (pos.x + halfExtents.x < mesh.boundsMin.x || pos.x - halfExtents.x > mesh.boundsMax.x ||
        pos.y + halfExtents.y < mesh.boundsMin.y || pos.y - halfExtents.y > mesh.boundsMax.y ||
        pos.z + halfExtents.z < mesh.boundsMin.z || pos.z - halfExtents.z > mesh.boundsMax.z)
        return;

    // Iterative aggregated depenetration to reduce jitter on curved surfaces.
    //
    // The naïve approach — apply each overlapping triangle's MTV one at a time —
    // jitters when the entity straddles many triangles at once on a curved
    // surface (e.g. inside a tube): each push moves the entity into a slightly
    // different overlap with the *next* triangle, so the net motion oscillates.
    //
    // Instead, in each pass we:
    //   1. Find every overlapping triangle (BVH-walk) at the *same* position.
    //   2. Sum their normalised MTV directions (a Quake-style "average normal"
    //      across all simultaneous contacts).
    //   3. Push once along that averaged direction by the deepest single MTV
    //      magnitude (plus the pushback bias).
    //
    // For a curved mesh, the averaged direction points smoothly out of the
    // surface (e.g. radially outward from a tube's axis) instead of fighting
    // between adjacent triangle normals.  For a corner where two faces meet,
    // it points roughly diagonally and a few iterations clear the residual
    // overlap on each face.  k_maxPasses caps the work even for pathological
    // self-intersecting meshes.
    constexpr int k_maxPasses = 4;

    for (int pass = 0; pass < k_maxPasses; ++pass) {
        glm::vec3 sumDir(0.0f);
        float maxDepth = 0.0f;
        int overlapCount = 0;

        int stack[64];
        int stackPtr = 0;
        stack[0] = 0;

        while (stackPtr >= 0) {
            const int nodeIdx = stack[stackPtr--];
            const BVHNode& node = mesh.bvhNodes[static_cast<size_t>(nodeIdx)];

            // Cull this node by Minkowski-expanded AABB overlap test.
            const glm::vec3 expMin = node.boundsMin - halfExtents;
            const glm::vec3 expMax = node.boundsMax + halfExtents;
            if (pos.x < expMin.x || pos.x > expMax.x || pos.y < expMin.y || pos.y > expMax.y || pos.z < expMin.z ||
                pos.z > expMax.z)
                continue;

            if (node.count > 0) {
                // Leaf — accumulate MTV contributions from every overlapping triangle.
                for (int i = node.leftFirst; i < node.leftFirst + node.count; ++i) {
                    const uint32_t ti = mesh.triIndices[static_cast<size_t>(i)];
                    const glm::vec3& v0 = mesh.vertices[mesh.indices[ti * 3 + 0]];
                    const glm::vec3& v1 = mesh.vertices[mesh.indices[ti * 3 + 1]];
                    const glm::vec3& v2 = mesh.vertices[mesh.indices[ti * 3 + 2]];

                    glm::vec3 mtv;
                    if (!aabbVsTriMTV(pos, halfExtents, v0, v1, v2, mtv))
                        continue;

                    const float depth = glm::length(mtv);
                    if (depth < 1e-6f)
                        continue;          // numerical floor — already touching

                    sumDir += mtv / depth; // unit direction; sum smooths across adjacent tris
                    maxDepth = std::max(maxDepth, depth);
                    ++overlapCount;
                }
            } else {
                stack[++stackPtr] = node.leftFirst;
                stack[++stackPtr] = node.leftFirst + 1;
            }
        }

        if (overlapCount == 0)
            return; // fully separated — done

        const float dirLen = glm::length(sumDir);
        if (dirLen < 1e-6f)
            return; // contributions cancel out (rare; happens only if entity is
                    // perfectly centred inside a closed mesh and all radial pushes
                    // cancel — no useful direction to push, give up gracefully)

        const glm::vec3 dir = sumDir / dirLen;

        // Push along the averaged direction by enough to clear the deepest single
        // overlap (plus the pushback bias).  Other simultaneous overlaps may not
        // be fully cleared if their MTV directions diverge significantly from the
        // average — those get caught by the next pass.
        pos += dir * (maxDepth + pushback);

        // Cancel velocity component flowing INTO the contact.  Using the averaged
        // direction means the entity slides along the *average* surface normal,
        // which is the smoothest behaviour on curved meshes.
        const float into = glm::dot(vel, dir);
        if (into < 0.0f)
            vel -= dir * into;
    }
}

} // namespace physics
