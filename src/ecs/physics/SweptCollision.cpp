#include "SweptCollision.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace physics
{

HitResult sweepAABB(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, std::span<const Plane> planes)
{
    HitResult result; // hit=false, tFirst=1.0 by default

    for (const Plane& plane : planes) {
        // Expand the plane outward by the AABB's extent in the plane's normal direction
        // (Minkowski sum). This lets us treat the sweep as a point vs. expanded plane.
        // r = how far the AABB "sticks out" in the normal direction.
        const float k_r = std::abs(plane.normal.x) * halfExtents.x + std::abs(plane.normal.y) * halfExtents.y +
                          std::abs(plane.normal.z) * halfExtents.z;

        // Signed distances of the AABB centre from the (unexpanded) plane.
        const float k_distStart = glm::dot(plane.normal, start) - plane.distance;
        const float k_distEnd = glm::dot(plane.normal, end) - plane.distance;

        // Skip only if the entity is clearly inside the solid (not just touching).
        // Entities exactly AT the surface (k_distStart == k_r) must NOT be skipped —
        // they need a t=0 hit so grounded is set and velocity is clipped.
        if (k_distStart < k_r)
            continue;

        // Skip if not moving toward the plane (moving away or parallel).
        if (k_distEnd >= k_distStart)
            continue;

        // Time at which the front face of the AABB reaches the expanded plane.
        // Derivation: solve (k_distStart - k_r) + t*(k_distEnd - k_distStart) = 0
        const float k_t = (k_distStart - k_r) / (k_distStart - k_distEnd);

        if (k_t >= 0.0f && k_t < result.tFirst) {
            result.hit = true;
            result.tFirst = k_t;
            result.normal = plane.normal;
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// sweepAABBvsBox — Swept AABB vs static AABB (Minkowski difference + slab test)
//
// Expand the static box by the moving AABB's half-extents, then ray-test
// the AABB centre against the expanded box.  The first slab entry gives
// the collision time and the face normal.
// ═══════════════════════════════════════════════════════════════════════════

HitResult sweepAABBvsBox(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldAABB& box)
{
    HitResult result;

    // Expand the static box by the moving AABB's half-extents (Minkowski sum).
    const glm::vec3 k_expMin = box.min - halfExtents;
    const glm::vec3 k_expMax = box.max + halfExtents;

    // If the centre already starts inside the expanded box, skip.
    // Depenetration handles this case separately.
    if (start.x >= k_expMin.x && start.x <= k_expMax.x && start.y >= k_expMin.y && start.y <= k_expMax.y &&
        start.z >= k_expMin.z && start.z <= k_expMax.z)
        return result;

    const glm::vec3 k_delta = end - start;

    // Slab intersection on each axis: find the entry/exit interval.
    float tEntry = -1e30f; // latest entry (across all axes)
    float tExit = 1e30f;   // earliest exit
    glm::vec3 hitNormal{0.0f};

    for (int axis = 0; axis < 3; ++axis) {
        const float k_lo = k_expMin[axis];
        const float k_hi = k_expMax[axis];

        if (std::abs(k_delta[axis]) < 1e-8f) {
            // Parallel to this slab — must be between k_lo and k_hi.
            if (start[axis] < k_lo || start[axis] > k_hi)
                return result; // miss
        } else {
            const float k_invD = 1.0f / k_delta[axis];
            float t1 = (k_lo - start[axis]) * k_invD; // entry on min side
            float t2 = (k_hi - start[axis]) * k_invD; // entry on max side

            // Normals for each slab face.
            glm::vec3 n1{0.0f};
            n1[axis] = -1.0f; // hit the min face → outward normal points negative
            glm::vec3 n2{0.0f};
            n2[axis] = 1.0f;  // hit the max face → outward normal points positive

            if (t1 > t2) {
                std::swap(t1, t2);
                std::swap(n1, n2);
            }

            if (t1 > tEntry) {
                tEntry = t1;
                hitNormal = n1;
            }
            if (t2 < tExit) {
                tExit = t2;
            }

            if (tEntry > tExit || tExit < 0.0f)
                return result; // miss
        }
    }

    // Must hit within the sweep interval [0, 1).
    if (tEntry >= 0.0f && tEntry < 1.0f && tEntry < result.tFirst) {
        result.hit = true;
        result.tFirst = tEntry;
        result.normal = hitNormal;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// sweepAABBvsBrush — Swept AABB vs convex brush (set of bounding planes)
//
// A convex brush is the intersection of half-spaces.  The sweep enters
// the brush when it simultaneously crosses all planes from outside to
// inside.  We track the latest entry and earliest exit; if entry < exit
// and entry ∈ [0, 1), the sweep hits the brush.
// ═══════════════════════════════════════════════════════════════════════════

HitResult sweepAABBvsBrush(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldBrush& brush)
{
    HitResult result;

    float tEntry = -1e30f;
    float tExit = 1e30f;
    glm::vec3 hitNormal{0.0f, 1.0f, 0.0f};
    bool startsOutside = false;

    for (int i = 0; i < brush.planeCount; ++i) {
        const Plane& plane = brush.planes[i];

        // Expand plane by AABB extent in the normal direction (Minkowski sum).
        const float k_r = std::abs(plane.normal.x) * halfExtents.x + std::abs(plane.normal.y) * halfExtents.y +
                          std::abs(plane.normal.z) * halfExtents.z;

        // Adjusted distances: positive = outside (free space), negative = inside (solid).
        const float k_adjStart = glm::dot(plane.normal, start) - plane.distance - k_r;
        const float k_adjEnd = glm::dot(plane.normal, end) - plane.distance - k_r;

        if (k_adjStart > 0.0f)
            startsOutside = true;

        // Both endpoints outside this plane → sweep misses the brush entirely.
        if (k_adjStart > 0.0f && k_adjEnd > 0.0f)
            return result;

        // Both endpoints inside this plane → this plane doesn't constrain the interval.
        if (k_adjStart <= 0.0f && k_adjEnd <= 0.0f)
            continue;

        // Crossing this plane — compute intersection time.
        const float k_t = k_adjStart / (k_adjStart - k_adjEnd);

        if (k_adjStart > 0.0f) {
            // Entering the solid side of this plane.
            if (k_t > tEntry) {
                tEntry = k_t;
                hitNormal = plane.normal;
            }
        } else {
            // Exiting the solid side.
            if (k_t < tExit) {
                tExit = k_t;
            }
        }
    }

    // Must start outside the brush (depenetration handles the inside case).
    if (!startsOutside)
        return result;

    // Entry must be before exit, and within sweep range.
    if (tEntry < tExit && tEntry >= 0.0f && tEntry < 1.0f) {
        result.hit = true;
        result.tFirst = tEntry;
        result.normal = hitNormal;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// sweepAll — Test against all world geometry, return earliest hit
// ═══════════════════════════════════════════════════════════════════════════

HitResult sweepAll(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldGeometry& world)
{
    HitResult best = sweepAABB(halfExtents, start, end, world.planes);

    for (const WorldAABB& box : world.boxes) {
        const HitResult k_hr = sweepAABBvsBox(halfExtents, start, end, box);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    for (const WorldBrush& brush : world.brushes) {
        const HitResult k_hr = sweepAABBvsBrush(halfExtents, start, end, brush);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    return best;
}

// ═══════════════════════════════════════════════════════════════════════════
// sphereCast — Swept sphere vs all world geometry
//
// Expands each piece of geometry by the sphere radius (Minkowski sum), then
// tests the sphere centre as a point/ray against the expanded geometry.
// This gives exact results for planes and brushes, and slightly conservative
// results for AABB corners (inflated box instead of rounded box), which is
// acceptable and even desirable for wall detection generosity.
// ═══════════════════════════════════════════════════════════════════════════

SphereHitResult sphereCast(float radius, glm::vec3 start, glm::vec3 end, const WorldGeometry& world)
{
    SphereHitResult best;
    const glm::vec3 k_delta = end - start;

    // ── Test against infinite planes ────────────────────────────────────
    for (const Plane& plane : world.planes) {
        const float k_distStart = glm::dot(plane.normal, start) - plane.distance;
        const float k_distEnd = glm::dot(plane.normal, end) - plane.distance;

        if (k_distStart < radius)
            continue; // starts inside
        if (k_distEnd >= k_distStart)
            continue; // moving away

        const float k_t = (k_distStart - radius) / (k_distStart - k_distEnd);
        if (k_t >= 0.0f && k_t < best.t) {
            best.hit = true;
            best.t = k_t;
            best.normal = plane.normal;
            best.point = glm::mix(start, end, k_t) - plane.normal * radius;
        }
    }

    // ── Test against AABBs (inflated by sphere radius) ──────────────────
    for (const WorldAABB& box : world.boxes) {
        const glm::vec3 k_expMin = box.min - glm::vec3(radius);
        const glm::vec3 k_expMax = box.max + glm::vec3(radius);

        // Skip if starting inside the inflated box.
        if (start.x >= k_expMin.x && start.x <= k_expMax.x && start.y >= k_expMin.y && start.y <= k_expMax.y &&
            start.z >= k_expMin.z && start.z <= k_expMax.z)
            continue;

        float tEntry = -1e30f;
        float tExit = 1e30f;
        glm::vec3 hitN{0.0f};
        bool miss = false;

        for (int axis = 0; axis < 3 && !miss; ++axis) {
            const float k_lo = k_expMin[axis];
            const float k_hi = k_expMax[axis];

            if (std::abs(k_delta[axis]) < 1e-8f) {
                if (start[axis] < k_lo || start[axis] > k_hi)
                    miss = true;
            } else {
                const float k_invD = 1.0f / k_delta[axis];
                float t1 = (k_lo - start[axis]) * k_invD;
                float t2 = (k_hi - start[axis]) * k_invD;

                glm::vec3 n1{0.0f};
                n1[axis] = -1.0f;
                glm::vec3 n2{0.0f};
                n2[axis] = 1.0f;

                if (t1 > t2) {
                    std::swap(t1, t2);
                    std::swap(n1, n2);
                }
                if (t1 > tEntry) {
                    tEntry = t1;
                    hitN = n1;
                }
                if (t2 < tExit)
                    tExit = t2;
                if (tEntry > tExit || tExit < 0.0f)
                    miss = true;
            }
        }

        if (!miss && tEntry >= 0.0f && tEntry < best.t) {
            best.hit = true;
            best.t = tEntry;
            best.normal = hitN;
            best.point = start + k_delta * tEntry;
        }
    }

    // ── Test against brushes (each plane expanded by radius) ────────────
    for (const WorldBrush& brush : world.brushes) {
        float tEntry = -1e30f;
        float tExit = 1e30f;
        glm::vec3 hitN{0.0f, 1.0f, 0.0f};
        bool startsOutside = false;
        bool miss = false;

        for (int i = 0; i < brush.planeCount && !miss; ++i) {
            const Plane& p = brush.planes[i];
            // For a sphere, r = radius for every plane (sphere is symmetric).
            const float k_adjStart = glm::dot(p.normal, start) - p.distance - radius;
            const float k_adjEnd = glm::dot(p.normal, end) - p.distance - radius;

            if (k_adjStart > 0.0f)
                startsOutside = true;
            if (k_adjStart > 0.0f && k_adjEnd > 0.0f) {
                miss = true;
                break;
            }
            if (k_adjStart <= 0.0f && k_adjEnd <= 0.0f)
                continue;

            const float k_t = k_adjStart / (k_adjStart - k_adjEnd);
            if (k_adjStart > 0.0f) {
                if (k_t > tEntry) {
                    tEntry = k_t;
                    hitN = p.normal;
                }
            } else {
                if (k_t < tExit)
                    tExit = k_t;
            }
        }

        if (!miss && startsOutside && tEntry < tExit && tEntry >= 0.0f && tEntry < best.t) {
            best.hit = true;
            best.t = tEntry;
            best.normal = hitN;
            best.point = start + k_delta * tEntry - hitN * radius;
        }
    }

    return best;
}

} // namespace physics
