/// @file TriMeshCollision.cpp
/// @brief BVH builder, edge-welding cooker, and Voronoi-clipped runtime
///        primitives for AABB-vs-triangle-mesh collision.
///
/// Phase 2 of the physics roadmap.  Replaces the older SAT-MTV per-triangle
/// path (which produced "ghost contacts" on internal edges of triangulated
/// flat surfaces, requiring multi-pass MTV averaging to mask the noise) with:
///   1. A cook-time welding pass (`weldTriMesh`) that classifies every
///      shared edge as convex / concave / coplanar, marking only convex
///      edges and their incident vertices as "active".
///   2. Runtime primitives that find the closest Voronoi feature on a
///      triangle to the AABB centre, discard contacts on inactive features,
///      and otherwise produce a face-normal MTV.  See Ericson, *Real-Time
///      Collision Detection* §5.1.5 for the closest-feature algorithm, and
///      Jolt's `MeshShape::sCollideConvex` for the discard logic.

#include "TriMeshCollision.hpp"

#include "ecs/physics/DebugCollisionDraw.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/geometric.hpp>
#include <unordered_map>

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

// Voronoi-region helpers

/// @brief Which Voronoi region of a triangle a query point's closest projection
/// falls into.  Used to clip contacts against inactive (welded) features.
enum class TriRegion : uint8_t
{
    Face = 0,
    Edge0 = 1, ///< Edge v0→v1
    Edge1 = 2, ///< Edge v1→v2
    Edge2 = 3, ///< Edge v2→v0
    Vert0 = 4,
    Vert1 = 5,
    Vert2 = 6,
};

/// @brief Closest point on triangle (a, b, c) to query point p, plus the
/// Voronoi region tag for the chosen feature.  Ericson, *RTCD* §5.1.5.
TriRegion closestPointOnTriangle(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3& outClosest)
{
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;
    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        outClosest = a;
        return TriRegion::Vert0;
    }

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        outClosest = b;
        return TriRegion::Vert1;
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        outClosest = a + ab * v;
        return TriRegion::Edge0;
    }

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        outClosest = c;
        return TriRegion::Vert2;
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        outClosest = a + ac * w;
        return TriRegion::Edge2;
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        outClosest = b + (c - b) * w;
        return TriRegion::Edge1;
    }

    // Inside face.
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    outClosest = a + ab * v + ac * w;
    return TriRegion::Face;
}

// Voronoi-clipped depenetration MTV: returns true iff there is a valid contact
// between an AABB centred at `center` (half-extents `he`) and the triangle.
// `faceN` is the triangle's cooked face normal (CCW); `edgeFlags` and
// `vertFlags` are the cooked active-feature masks.  On success, `outMtv` is the
// face-normal MTV that pushes the AABB out to the free side of the triangle.
bool aabbVsTriVoronoi(glm::vec3 center,
                     glm::vec3 he,
                     glm::vec3 v0,
                     glm::vec3 v1,
                     glm::vec3 v2,
                     glm::vec3 faceN,
                     uint8_t edgeFlags,
                     uint8_t vertFlags,
                     glm::vec3& outMtv)
{
    // 1. Plane test: is the AABB even straddling the triangle's plane?
    //    `r` is the AABB's projected half-extent on the normal (Minkowski radius).
    const float r = std::abs(faceN.x) * he.x + std::abs(faceN.y) * he.y + std::abs(faceN.z) * he.z;
    const float s = glm::dot(faceN, center - v0); // signed distance to plane
    if (std::abs(s) > r)
        return false;

    // 2. Find the closest feature on the bounded triangle.  Used both to
    //    detect ghost contacts ("AABB doesn't actually reach the closest
    //    point") and — historically — to discard inactive welded features.
    //    The discard behaviour was too aggressive: indoor FPS maps have
    //    *concave* wall-floor corners where BOTH neighbours' closest
    //    feature is the shared welded edge.  Discarding both leaves the
    //    player wedged with zero depenetration push, which is the
    //    wallrun-into-corner phase-through bug.
    //
    //    New behaviour (Bullet `btAdjustInternalEdgeContacts`-style):
    //    inactive features are NOT discarded — the face normal is used as
    //    the contact direction.  Two adjacent triangles sharing an
    //    inactive edge still produce mutually-consistent MTVs because
    //    their face normals either agree (coplanar internal seam) or
    //    aggregate to the corner bisector (concave fold).  Ghost contacts
    //    at far edges are still suppressed by the AABB-reach check below.
    glm::vec3 closest;
    const TriRegion region = closestPointOnTriangle(center, v0, v1, v2, closest);

    // 3. For non-face features, verify the AABB actually reaches the
    //    closest point.  This is the *only* place we discard contacts —
    //    the plane test passed, but the triangle's bounded region is too
    //    far from the AABB to actually overlap.
    if (region != TriRegion::Face) {
        const glm::vec3 d = closest - center;
        const float dLenSq = glm::dot(d, d);
        if (dLenSq > 1e-12f) {
            const float dLen = std::sqrt(dLenSq);
            const glm::vec3 unitD = d / dLen;
            const float rD = std::abs(unitD.x) * he.x + std::abs(unitD.y) * he.y + std::abs(unitD.z) * he.z;
            // Small slack for floating-point inexactness near the boundary.
            if (dLen > rD + 1e-4f)
                return false;
        }
    }

    // 4. Phase-through guard.  The depth formula `r - s` below can push
    //    by up to 2r when `s < 0` (player center on the back side of a
    //    one-sided triangle), which teleports through coplanar
    //    opposing-normal duplicates.  The welder marks the relevant
    //    feature inactive in that case (cosTheta ≈ -1 for the partner
    //    → signedDihedral == 0 → not active).  Skip the contact when
    //    the closest feature is in such an inactive region — the
    //    front-facing partner triangle owns the real contact, and the
    //    bump loop catches genuine tunneling on the next sweep.  Face-
    //    region contacts use the heuristic "no edge of this triangle
    //    is active", which also catches back-to-back duplicates
    //    (whose shared edges are all inactive).
    if (s < 0.0f) {
        bool inactive = false;
        switch (region) {
        case TriRegion::Face:  inactive = (edgeFlags == 0u); break;
        case TriRegion::Edge0: inactive = (edgeFlags & 0x1u) == 0u; break;
        case TriRegion::Edge1: inactive = (edgeFlags & 0x2u) == 0u; break;
        case TriRegion::Edge2: inactive = (edgeFlags & 0x4u) == 0u; break;
        case TriRegion::Vert0: inactive = (vertFlags & 0x1u) == 0u; break;
        case TriRegion::Vert1: inactive = (vertFlags & 0x2u) == 0u; break;
        case TriRegion::Vert2: inactive = (vertFlags & 0x4u) == 0u; break;
        }
        if (inactive)
            return false;
    }

    // 5. Face-normal MTV.  Push along the face normal so coplanar triangles
    //    sharing an inactive edge produce mutually consistent MTV
    //    directions (no fight at the seam), and so concave corners get
    //    two face-normal MTVs that aggregate to the bisector.
    const float depth = r - s;
    if (depth <= 0.0f)
        return false;
    outMtv = faceN * depth;
    return true;
}

// Voronoi-clipped swept-AABB-vs-triangle.  Performs a ray-vs-expanded-plane
// test (Minkowski-expanded by the AABB along the face normal), then validates
// the contact point against the triangle's Voronoi regions and welding flags.
HitResult sweepAABBvsTriangle(glm::vec3 halfExtents,
                              glm::vec3 start,
                              glm::vec3 end,
                              glm::vec3 v0,
                              glm::vec3 v1,
                              glm::vec3 v2,
                              glm::vec3 faceN,
                              uint8_t edgeFlags,
                              uint8_t vertFlags)
{
    HitResult result;

    // Orient the face normal so it points toward the start of the sweep —
    // the contact is on whichever side the sweeper is approaching from.
    glm::vec3 n = faceN;
    if (glm::dot(n, start - v0) < 0.0f)
        n = -n;

    const float r = std::abs(n.x) * halfExtents.x + std::abs(n.y) * halfExtents.y + std::abs(n.z) * halfExtents.z;
    const float distStart = glm::dot(n, start - v0);
    const float distEnd = glm::dot(n, end - v0);

    // Must start outside (in front of) the expanded plane and be moving toward it.
    if (distStart < r)
        return result;
    if (distEnd >= distStart)
        return result;

    const float t = (distStart - r) / (distStart - distEnd);
    if (t < 0.0f || t >= 1.0f)
        return result;

    // Contact-time AABB centre.
    const glm::vec3 contactPos = start + (end - start) * t;

    // Validate that the AABB at contact time actually reaches the triangle.
    // For projections inside the bounded triangle (Face region) this is
    // implied by the plane test.  For projections on edges / vertices we
    // need the AABB-reach check — otherwise we'd fire false hits on
    // triangles whose bounded body is outside the AABB's projected extent.
    //
    // We deliberately do NOT discard contacts based on the edge / vertex
    // *active* flag.  For coplanar internal seams, both adjacent
    // triangles report the same hit time with the same face normal —
    // either is correct.  For concave corners (indoor wall-floor seams,
    // for example) both report the same hit time with different face
    // normals — the bump loop's velocity clip handles the bisector
    // afterwards.  Discarding "inactive" features here was the root
    // cause of the wallrun-into-corner phase-through bug — a contact
    // discarded by both neighbours left the sweep blind to the corner
    // entirely.
    glm::vec3 closest;
    const TriRegion region = closestPointOnTriangle(contactPos, v0, v1, v2, closest);
    if (region != TriRegion::Face) {
        const glm::vec3 d = closest - contactPos;
        const float dLenSq = glm::dot(d, d);
        if (dLenSq > 1e-12f) {
            const float dLen = std::sqrt(dLenSq);
            const glm::vec3 unitD = d / dLen;
            const float rD =
                std::abs(unitD.x) * halfExtents.x + std::abs(unitD.y) * halfExtents.y + std::abs(unitD.z) * halfExtents.z;
            if (dLen > rD + 1e-3f)
                return result;
        }
    }
    (void)edgeFlags;
    (void)vertFlags;

    result.hit = true;
    result.tFirst = t;
    result.normal = n;
    return result;
}

// Welding helpers

/// @brief Canonical uint64 key for an undirected edge between two vertex indices.
inline uint64_t edgeKey(uint32_t a, uint32_t b) noexcept
{
    if (a > b)
        std::swap(a, b);
    return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
}

/// @brief Per-edge adjacency record built during welding.  Stores up to two
/// incident triangles; non-manifold edges (>2 triangles) bump `count` past 2.
struct EdgeRecord
{
    uint32_t tri[2] = {UINT32_MAX, UINT32_MAX};
    uint8_t edgeIdx[2] = {0, 0};
    uint8_t count = 0;
};

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

void weldTriMesh(WorldTriMesh& mesh, float coplanarTolerance)
{
    const uint32_t triCount = static_cast<uint32_t>(mesh.indices.size() / 3);
    mesh.faceNormals.assign(triCount, glm::vec3{0.0f, 1.0f, 0.0f});
    mesh.edgeActive.assign(triCount, 0u);
    mesh.vertActive.assign(triCount, 0u);
    if (triCount == 0)
        return;

    // 1. Face normals.  Degenerate triangles (collinear vertices) get a
    //    fallback +Y normal — they never produce contacts because their
    //    plane test always fails the |s| ≤ r check, so the fallback value
    //    only matters for indexing safety.
    for (uint32_t t = 0; t < triCount; ++t) {
        const glm::vec3& v0 = mesh.vertices[mesh.indices[t * 3 + 0]];
        const glm::vec3& v1 = mesh.vertices[mesh.indices[t * 3 + 1]];
        const glm::vec3& v2 = mesh.vertices[mesh.indices[t * 3 + 2]];
        const glm::vec3 raw = glm::cross(v1 - v0, v2 - v0);
        const float lenSq = glm::dot(raw, raw);
        if (lenSq > 1e-12f)
            mesh.faceNormals[t] = raw / std::sqrt(lenSq);
    }

    // 2. Build edge → triangle adjacency.  Order is deterministic because we
    //    iterate triangles in index order; the hash map's iteration order is
    //    NOT used (we only look records up by key in step 4).
    std::unordered_map<uint64_t, EdgeRecord> adjacency;
    adjacency.reserve(static_cast<size_t>(triCount) * 3u);

    for (uint32_t t = 0; t < triCount; ++t) {
        const uint32_t v[3] = {
            mesh.indices[t * 3 + 0],
            mesh.indices[t * 3 + 1],
            mesh.indices[t * 3 + 2],
        };
        for (int e = 0; e < 3; ++e) {
            const uint64_t k = edgeKey(v[e], v[(e + 1) % 3]);
            EdgeRecord& rec = adjacency[k];
            if (rec.count < 2) {
                rec.tri[rec.count] = t;
                rec.edgeIdx[rec.count] = static_cast<uint8_t>(e);
            }
            ++rec.count;
        }
    }

    // 3. Classify each edge of each triangle.
    const float cosThresh = std::cos(coplanarTolerance);

    for (uint32_t t = 0; t < triCount; ++t) {
        const uint32_t v[3] = {
            mesh.indices[t * 3 + 0],
            mesh.indices[t * 3 + 1],
            mesh.indices[t * 3 + 2],
        };
        const glm::vec3& nA = mesh.faceNormals[t];

        for (int e = 0; e < 3; ++e) {
            const uint64_t k = edgeKey(v[e], v[(e + 1) % 3]);
            const auto it = adjacency.find(k);
            // Every edge must have a record (we inserted them all in step 2).
            const EdgeRecord& rec = it->second;

            bool active = false;
            if (rec.count == 1u) {
                // Boundary edge — always a real surface feature.
                active = true;
            } else if (rec.count == 2u) {
                // Find the *other* triangle and compare face normals.
                const uint32_t other = (rec.tri[0] == t) ? rec.tri[1] : rec.tri[0];
                const glm::vec3& nB = mesh.faceNormals[other];
                const float cosTheta = glm::dot(nA, nB);

                // Convex edge ⇔ cross(nA, nB) points along the edge direction
                // (CCW orientation).  This works regardless of which triangle
                // "owns" the canonical edge because both observations use the
                // same edge vector when classified from either side — the sign
                // flips with the cross-product orientation.
                const glm::vec3& va = mesh.vertices[v[e]];
                const glm::vec3& vb = mesh.vertices[v[(e + 1) % 3]];
                const glm::vec3 edgeVec = vb - va;
                const float signedDihedral = glm::dot(glm::cross(nA, nB), edgeVec);

                active = (signedDihedral > 0.0f) && (cosTheta < cosThresh);
            } else {
                // Non-manifold edge (>2 incident triangles).  Treat as active —
                // it's a real geometric feature even if it's pathological.
                active = true;
            }

            if (active)
                mesh.edgeActive[t] = static_cast<uint8_t>(mesh.edgeActive[t] | (1u << e));
        }
    }

    // 4. Vertex flags derived from edge flags.  Vertex i is active in tri t
    //    iff either of t's two edges incident to i is active.
    //      Vertex 0 ⇐ edges 0 (v0→v1) and 2 (v2→v0)
    //      Vertex 1 ⇐ edges 0 and 1 (v1→v2)
    //      Vertex 2 ⇐ edges 1 and 2
    for (uint32_t t = 0; t < triCount; ++t) {
        const uint8_t e = mesh.edgeActive[t];
        uint8_t vMask = 0;
        if (e & (1u << 0))
            vMask |= static_cast<uint8_t>((1u << 0) | (1u << 1));
        if (e & (1u << 1))
            vMask |= static_cast<uint8_t>((1u << 1) | (1u << 2));
        if (e & (1u << 2))
            vMask |= static_cast<uint8_t>((1u << 2) | (1u << 0));
        mesh.vertActive[t] = vMask;
    }
}

HitResult sweepAABBvsTriMesh(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldTriMesh& mesh)
{
    HitResult best;
    if (mesh.bvhNodes.empty() || mesh.faceNormals.empty())
        return best;

    const glm::vec3 delta = end - start;

    // Quick reject against whole-mesh AABB.
    if (!sweptAABBOverlapsAABB(halfExtents, start, delta, mesh.boundsMin, mesh.boundsMax, 1.0f))
        return best;

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
            for (int i = node.leftFirst; i < node.leftFirst + node.count; ++i) {
                const uint32_t ti = mesh.triIndices[static_cast<size_t>(i)];
                const glm::vec3& v0 = mesh.vertices[mesh.indices[ti * 3 + 0]];
                const glm::vec3& v1 = mesh.vertices[mesh.indices[ti * 3 + 1]];
                const glm::vec3& v2 = mesh.vertices[mesh.indices[ti * 3 + 2]];

                HitResult hr = sweepAABBvsTriangle(halfExtents,
                                                   start,
                                                   end,
                                                   v0,
                                                   v1,
                                                   v2,
                                                   mesh.faceNormals[ti],
                                                   mesh.edgeActive[ti],
                                                   mesh.vertActive[ti]);
                if (hr.hit && hr.tFirst < best.tFirst) {
                    // Per-triangle material if cooked, else mesh-wide default.
                    if (ti < mesh.triangleMaterials.size())
                        hr.surfaceType = static_cast<SurfaceType>(mesh.triangleMaterials[ti]);
                    else
                        hr.surfaceType = mesh.defaultSurface;
                    best = hr;
                    if (debug::isEnabled()) {
                        const glm::vec3 hitPos = start + (end - start) * hr.tFirst;
                        const float r = std::abs(hr.normal.x) * halfExtents.x +
                                        std::abs(hr.normal.y) * halfExtents.y +
                                        std::abs(hr.normal.z) * halfExtents.z;
                        debug::pushSweepContact(
                            hitPos - hr.normal * r, hr.normal, debug::ContactSource::TriMeshSweep, ti);
                    }
                }
            }
        } else {
            stack[++stackPtr] = node.leftFirst;
            stack[++stackPtr] = node.leftFirst + 1;
        }
    }

    return best;
}

void depenetrateAABBvsTriMesh(
    glm::vec3& pos, glm::vec3& vel, glm::vec3 halfExtents, const WorldTriMesh& mesh, float pushback)
{
    if (mesh.bvhNodes.empty() || mesh.faceNormals.empty())
        return;

    // Quick reject: AABB must overlap the Minkowski-expanded whole-mesh bounds.
    if (pos.x + halfExtents.x < mesh.boundsMin.x || pos.x - halfExtents.x > mesh.boundsMax.x ||
        pos.y + halfExtents.y < mesh.boundsMin.y || pos.y - halfExtents.y > mesh.boundsMax.y ||
        pos.z + halfExtents.z < mesh.boundsMin.z || pos.z - halfExtents.z > mesh.boundsMax.z)
        return;

    // Voronoi-clipped depenetration with bounded iteration.
    //
    // Per-triangle MTVs are face-normal (Phase 2 welding) so adjacent
    // coplanar welded triangles can't produce ghost contacts that fight
    // each other.  But a single aggregation pass is geometrically
    // insufficient at concave / convex corners where TWO orthogonal face
    // normals meet: averaging produces a diagonal direction, and pushing
    // by `maxDepth` along that diagonal leaves ~30 % residual penetration
    // on each face (the diagonal push falls short of fully clearing each
    // axis by a factor of `1 - cos(angle/2)` ≈ 0.293 for a 90° corner).
    //
    // The bump loop's "starts inside → skip" rule means residual
    // penetration after a single depen pass is fatal: next tick the
    // sweep won't detect the surface and the player phases through.
    // High-velocity actions (wallrun, double jump, grapple) jam the
    // player into corners hard enough that the residual matters.
    //
    // Fix: iterate up to k_maxPasses times.  Each pass aggregates the
    // *current* set of overlapping triangles and pushes once; convergence
    // is geometric (each pass reduces residual by the same ratio), so 4
    // passes clear corners down to < 1 % of their initial penetration —
    // well below the pushback epsilon.
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

            const glm::vec3 expMin = node.boundsMin - halfExtents;
            const glm::vec3 expMax = node.boundsMax + halfExtents;
            if (pos.x < expMin.x || pos.x > expMax.x || pos.y < expMin.y || pos.y > expMax.y ||
                pos.z < expMin.z || pos.z > expMax.z)
                continue;

            if (node.count > 0) {
                for (int i = node.leftFirst; i < node.leftFirst + node.count; ++i) {
                    const uint32_t ti = mesh.triIndices[static_cast<size_t>(i)];
                    const glm::vec3& v0 = mesh.vertices[mesh.indices[ti * 3 + 0]];
                    const glm::vec3& v1 = mesh.vertices[mesh.indices[ti * 3 + 1]];
                    const glm::vec3& v2 = mesh.vertices[mesh.indices[ti * 3 + 2]];

                    glm::vec3 mtv;
                    if (!aabbVsTriVoronoi(pos,
                                          halfExtents,
                                          v0,
                                          v1,
                                          v2,
                                          mesh.faceNormals[ti],
                                          mesh.edgeActive[ti],
                                          mesh.vertActive[ti],
                                          mtv))
                        continue;

                    const float depth = glm::length(mtv);
                    if (depth < 1e-6f)
                        continue;

                    const glm::vec3 mtvDir = mtv / depth;
                    sumDir += mtvDir;
                    maxDepth = std::max(maxDepth, depth);
                    ++overlapCount;

                    if (debug::isEnabled()) {
                        debug::pushDepenContact(pos, mtvDir, depth, debug::ContactSource::TriMeshDepen, ti);
                    }
                }
            } else {
                stack[++stackPtr] = node.leftFirst;
                stack[++stackPtr] = node.leftFirst + 1;
            }
        }

        if (overlapCount == 0)
            return; // fully separated — done in fewer than k_maxPasses

        const float dirLen = glm::length(sumDir);
        if (dirLen < 1e-6f)
            return; // contributions cancel — entity centred inside a closed mesh.

        const glm::vec3 dir = sumDir / dirLen;
        pos += dir * (maxDepth + pushback);

        // Cancel inward velocity component on every pass so a sliding
        // player loses normal-direction motion immediately instead of
        // grinding back into the surface for k_maxPasses-1 frames.
        const float into = glm::dot(vel, dir);
        if (into < 0.0f)
            vel -= dir * into;
    }
}

} // namespace physics
