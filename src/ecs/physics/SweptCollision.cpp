/// @file SweptCollision.cpp
/// @brief Implementation of swept AABB and sphere collision queries.

#include "SweptCollision.hpp"

#include "TriMeshCollision.hpp"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace physics
{

namespace
{

constexpr float k_contactEpsilon = 0.03125f; // 1/32 unit — matches Quake DIST_EPSILON.

/// @brief Closest point on segment `[a, b]` to point `p`.  Standard
/// parametric clamp; degenerate (zero-length) segments collapse to `a`.
glm::vec3 closestPointSegmentPoint(glm::vec3 a, glm::vec3 b, glm::vec3 p)
{
    const glm::vec3 ab = b - a;
    const float abLenSq = glm::dot(ab, ab);
    if (abLenSq < 1e-12f)
        return a;
    const float t = glm::clamp(glm::dot(p - a, ab) / abLenSq, 0.0f, 1.0f);
    return a + ab * t;
}

} // namespace

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
            result.surfaceType = plane.surfaceType;
        }
    }

    return result;
}

// sweepAABBvsBox
//
// Expand the static box by the moving AABB's half-extents, then ray-test
// the AABB centre against the expanded box. The first slab entry gives
// the collision time and the face normal.

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
        result.surfaceType = box.surfaceType;
    }

    return result;
}

// sweepAABBvsBrush
//
// A convex brush is the intersection of half-spaces. The sweep enters
// the brush when it simultaneously crosses all planes from outside to
// inside. We track the latest entry and earliest exit; if entry < exit
// and entry is in [0, 1), the sweep hits the brush.

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
        result.surfaceType = brush.surfaceType;
    }

    return result;
}

// sweepAABBvsCylinder

HitResult sweepAABBvsCylinder(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldCylinder& cyl)
{
    HitResult result;

    // Minkowski expansion
    const float k_effR = cyl.radius + std::max(halfExtents.x, halfExtents.z);
    const float k_yMin = cyl.base.y - halfExtents.y;
    const float k_yMax = cyl.base.y + cyl.height + halfExtents.y;

    const glm::vec3 k_delta = end - start;

    // --- Y slab ---
    float tYentry = -1e30f;
    float tYexit = 1e30f;
    bool yCapHitBottom = false; // true if Y entry is the bottom cap

    if (std::abs(k_delta.y) < 1e-8f) {
        if (start.y < k_yMin || start.y > k_yMax)
            return result; // parallel and outside
    } else {
        const float k_invDy = 1.0f / k_delta.y;
        float t1 = (k_yMin - start.y) * k_invDy;
        float t2 = (k_yMax - start.y) * k_invDy;
        bool t1IsBottom = true;
        if (t1 > t2) {
            std::swap(t1, t2);
            t1IsBottom = false;
        }
        tYentry = t1;
        tYexit = t2;
        yCapHitBottom = t1IsBottom;
    }

    // --- XZ circle ---
    const float k_ox = start.x - cyl.base.x;
    const float k_oz = start.z - cyl.base.z;
    const float k_dx = k_delta.x;
    const float k_dz = k_delta.z;

    const float k_a = k_dx * k_dx + k_dz * k_dz;
    const float k_b = 2.0f * (k_ox * k_dx + k_oz * k_dz);
    const float k_c = k_ox * k_ox + k_oz * k_oz - k_effR * k_effR;

    float tXZentry = -1e30f;
    float tXZexit = 1e30f;

    if (k_a < 1e-12f) {
        // Moving purely vertically — check if inside the circle
        if (k_c > 0.0f)
            return result; // outside circle, moving vertically -> miss
        // Inside circle, tXZ range is all of (-inf, +inf)
    } else {
        const float k_disc = k_b * k_b - 4.0f * k_a * k_c;
        if (k_disc < 0.0f)
            return result; // ray misses the infinite cylinder

        const float k_sqrtDisc = std::sqrt(k_disc);
        const float k_inv2a = 0.5f / k_a;
        tXZentry = (-k_b - k_sqrtDisc) * k_inv2a;
        tXZexit = (-k_b + k_sqrtDisc) * k_inv2a;
    }

    // --- Intersect the two intervals ---
    bool hitIsYcap = false;
    float tEntry;
    if (tYentry > tXZentry) {
        tEntry = tYentry;
        hitIsYcap = true;
    } else {
        tEntry = tXZentry;
        hitIsYcap = false;
    }
    const float tExit = std::min(tYexit, tXZexit);

    if (tEntry > tExit || tExit < 0.0f)
        return result;

    // Skip if starting inside
    if (tEntry < 0.0f)
        return result;

    if (tEntry >= 1.0f)
        return result;

    // --- Compute normal ---
    glm::vec3 hitNormal;
    if (hitIsYcap) {
        hitNormal = yCapHitBottom ? glm::vec3(0.0f, -1.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        // Side hit — horizontal normal from axis to hit point
        const glm::vec3 k_hitPos = start + k_delta * tEntry;
        hitNormal = glm::vec3(k_hitPos.x - cyl.base.x, 0.0f, k_hitPos.z - cyl.base.z);
        const float k_len = glm::length(hitNormal);
        if (k_len > 1e-6f)
            hitNormal /= k_len;
        else
            hitNormal = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    result.hit = true;
    result.tFirst = tEntry;
    result.normal = hitNormal;
    result.surfaceType = cyl.surfaceType;
    return result;
}

// sweepAABBvsSphere

HitResult sweepAABBvsSphere(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, const WorldSphere& sph)
{
    HitResult result;

    // Conservative Minkowski expansion: use max half-extent component.
    const float k_effR = sph.radius + std::max({halfExtents.x, halfExtents.y, halfExtents.z});

    const glm::vec3 k_oc = start - sph.center;
    const glm::vec3 k_delta = end - start;

    const float k_a = glm::dot(k_delta, k_delta);
    if (k_a < 1e-12f)
        return result; // not moving

    const float k_b = 2.0f * glm::dot(k_oc, k_delta);
    const float k_c = glm::dot(k_oc, k_oc) - k_effR * k_effR;

    // Starting inside — skip (depenetration handles)
    if (k_c <= 0.0f)
        return result;

    const float k_disc = k_b * k_b - 4.0f * k_a * k_c;
    if (k_disc < 0.0f)
        return result;

    const float k_t = (-k_b - std::sqrt(k_disc)) / (2.0f * k_a);

    if (k_t < 0.0f || k_t >= 1.0f)
        return result;

    // Normal: from sphere centre to hit point.
    const glm::vec3 k_hitPos = start + k_delta * k_t;
    glm::vec3 hitNormal = k_hitPos - sph.center;
    const float k_len = glm::length(hitNormal);
    if (k_len > 1e-6f)
        hitNormal /= k_len;
    else
        hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);

    result.hit = true;
    result.tFirst = k_t;
    result.normal = hitNormal;
    result.surfaceType = sph.surfaceType;
    return result;
}

// Capsule swept-collision primitives (Phase A of physics-future-path.md)
//
// EXACT for planes and brushes — the capsule's projected half-extent along a
// plane normal is `radius + halfHeight * |dot(up, n)|`, no approximation.
//
// CONSERVATIVE for boxes, cylinders, and spheres — we use the capsule's
// enclosing AABB and route through the same slab / quadratic math the AABB
// versions use.  The over-approximation is bounded by the capsule's corner
// curvature and is acceptable for the dev-arena primitives (real maps are
// trimesh, which has its own exact capsule path in TriMeshCollision.cpp).
// Tighter per-primitive capsule math is a Phase-B refinement.

HitResult sweepCapsuleVsPlanes(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, std::span<const Plane> planes)
{
    HitResult result;

    for (const Plane& plane : planes) {
        const float k_r = capsule.minkowskiExtent(plane.normal);
        const float k_distStart = glm::dot(plane.normal, start) - plane.distance;
        const float k_distEnd = glm::dot(plane.normal, end) - plane.distance;

        if (k_distStart < k_r)
            continue;
        if (k_distEnd >= k_distStart)
            continue;

        const float k_t = (k_distStart - k_r) / (k_distStart - k_distEnd);
        if (k_t >= 0.0f && k_t < result.tFirst) {
            result.hit = true;
            result.tFirst = k_t;
            result.normal = plane.normal;
            result.surfaceType = plane.surfaceType;
        }
    }

    return result;
}

HitResult sweepCapsuleVsBox(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldAABB& box)
{
    HitResult result;

    const glm::vec3 he = capsule.enclosingHalfExtents();
    const glm::vec3 k_expMin = box.min - he;
    const glm::vec3 k_expMax = box.max + he;

    if (start.x >= k_expMin.x && start.x <= k_expMax.x && start.y >= k_expMin.y && start.y <= k_expMax.y &&
        start.z >= k_expMin.z && start.z <= k_expMax.z)
        return result;

    const glm::vec3 k_delta = end - start;

    float tEntry = -1e30f;
    float tExit = 1e30f;
    glm::vec3 hitNormal{0.0f};

    for (int axis = 0; axis < 3; ++axis) {
        const float k_lo = k_expMin[axis];
        const float k_hi = k_expMax[axis];

        if (std::abs(k_delta[axis]) < 1e-8f) {
            if (start[axis] < k_lo || start[axis] > k_hi)
                return result;
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
                hitNormal = n1;
            }
            if (t2 < tExit)
                tExit = t2;

            if (tEntry > tExit || tExit < 0.0f)
                return result;
        }
    }

    if (tEntry >= 0.0f && tEntry < 1.0f && tEntry < result.tFirst) {
        result.hit = true;
        result.tFirst = tEntry;
        result.normal = hitNormal;
        result.surfaceType = box.surfaceType;
    }

    return result;
}

HitResult sweepCapsuleVsBrush(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldBrush& brush)
{
    HitResult result;

    float tEntry = -1e30f;
    float tExit = 1e30f;
    glm::vec3 hitNormal{0.0f, 1.0f, 0.0f};
    bool startsOutside = false;

    for (int i = 0; i < brush.planeCount; ++i) {
        const Plane& plane = brush.planes[i];
        const float k_r = capsule.minkowskiExtent(plane.normal);

        const float k_adjStart = glm::dot(plane.normal, start) - plane.distance - k_r;
        const float k_adjEnd = glm::dot(plane.normal, end) - plane.distance - k_r;

        if (k_adjStart > 0.0f)
            startsOutside = true;
        if (k_adjStart > 0.0f && k_adjEnd > 0.0f)
            return result;
        if (k_adjStart <= 0.0f && k_adjEnd <= 0.0f)
            continue;

        const float k_t = k_adjStart / (k_adjStart - k_adjEnd);

        if (k_adjStart > 0.0f) {
            if (k_t > tEntry) {
                tEntry = k_t;
                hitNormal = plane.normal;
            }
        } else {
            if (k_t < tExit)
                tExit = k_t;
        }
    }

    if (!startsOutside)
        return result;

    if (tEntry < tExit && tEntry >= 0.0f && tEntry < 1.0f) {
        result.hit = true;
        result.tFirst = tEntry;
        result.normal = hitNormal;
        result.surfaceType = brush.surfaceType;
    }

    return result;
}

HitResult sweepCapsuleVsCylinder(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldCylinder& cyl)
{
    HitResult result;

    const glm::vec3 he = capsule.enclosingHalfExtents();
    const float k_effR = cyl.radius + std::max(he.x, he.z);
    const float k_yMin = cyl.base.y - he.y;
    const float k_yMax = cyl.base.y + cyl.height + he.y;

    const glm::vec3 k_delta = end - start;

    float tYentry = -1e30f;
    float tYexit = 1e30f;
    bool yCapHitBottom = false;

    if (std::abs(k_delta.y) < 1e-8f) {
        if (start.y < k_yMin || start.y > k_yMax)
            return result;
    } else {
        const float k_invDy = 1.0f / k_delta.y;
        float t1 = (k_yMin - start.y) * k_invDy;
        float t2 = (k_yMax - start.y) * k_invDy;
        bool t1IsBottom = true;
        if (t1 > t2) {
            std::swap(t1, t2);
            t1IsBottom = false;
        }
        tYentry = t1;
        tYexit = t2;
        yCapHitBottom = t1IsBottom;
    }

    const float k_ox = start.x - cyl.base.x;
    const float k_oz = start.z - cyl.base.z;
    const float k_dx = k_delta.x;
    const float k_dz = k_delta.z;

    const float k_a = k_dx * k_dx + k_dz * k_dz;
    const float k_b = 2.0f * (k_ox * k_dx + k_oz * k_dz);
    const float k_c = k_ox * k_ox + k_oz * k_oz - k_effR * k_effR;

    float tXZentry = -1e30f;
    float tXZexit = 1e30f;

    if (k_a < 1e-12f) {
        if (k_c > 0.0f)
            return result;
    } else {
        const float k_disc = k_b * k_b - 4.0f * k_a * k_c;
        if (k_disc < 0.0f)
            return result;

        const float k_sqrtDisc = std::sqrt(k_disc);
        const float k_inv2a = 0.5f / k_a;
        tXZentry = (-k_b - k_sqrtDisc) * k_inv2a;
        tXZexit = (-k_b + k_sqrtDisc) * k_inv2a;
    }

    bool hitIsYcap = false;
    float tEntry;
    if (tYentry > tXZentry) {
        tEntry = tYentry;
        hitIsYcap = true;
    } else {
        tEntry = tXZentry;
        hitIsYcap = false;
    }
    const float tExit = std::min(tYexit, tXZexit);

    if (tEntry > tExit || tExit < 0.0f)
        return result;
    if (tEntry < 0.0f)
        return result;
    if (tEntry >= 1.0f)
        return result;

    glm::vec3 hitNormal;
    if (hitIsYcap) {
        hitNormal = yCapHitBottom ? glm::vec3(0.0f, -1.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    } else {
        const glm::vec3 k_hitPos = start + k_delta * tEntry;
        hitNormal = glm::vec3(k_hitPos.x - cyl.base.x, 0.0f, k_hitPos.z - cyl.base.z);
        const float k_len = glm::length(hitNormal);
        if (k_len > 1e-6f)
            hitNormal /= k_len;
        else
            hitNormal = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    result.hit = true;
    result.tFirst = tEntry;
    result.normal = hitNormal;
    result.surfaceType = cyl.surfaceType;
    return result;
}

HitResult sweepCapsuleVsSphere(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldSphere& sph)
{
    HitResult result;

    const glm::vec3 he = capsule.enclosingHalfExtents();
    const float k_effR = sph.radius + std::max({he.x, he.y, he.z});

    const glm::vec3 k_oc = start - sph.center;
    const glm::vec3 k_delta = end - start;

    const float k_a = glm::dot(k_delta, k_delta);
    if (k_a < 1e-12f)
        return result;

    const float k_b = 2.0f * glm::dot(k_oc, k_delta);
    const float k_c = glm::dot(k_oc, k_oc) - k_effR * k_effR;

    if (k_c <= 0.0f)
        return result;

    const float k_disc = k_b * k_b - 4.0f * k_a * k_c;
    if (k_disc < 0.0f)
        return result;

    const float k_t = (-k_b - std::sqrt(k_disc)) / (2.0f * k_a);

    if (k_t < 0.0f || k_t >= 1.0f)
        return result;

    const glm::vec3 k_hitPos = start + k_delta * k_t;
    glm::vec3 hitNormal = k_hitPos - sph.center;
    const float k_len = glm::length(hitNormal);
    if (k_len > 1e-6f)
        hitNormal /= k_len;
    else
        hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);

    result.hit = true;
    result.tFirst = k_t;
    result.normal = hitNormal;
    result.surfaceType = sph.surfaceType;
    return result;
}

// sweepAll

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

    for (const WorldCylinder& cyl : world.cylinders) {
        const HitResult k_hr = sweepAABBvsCylinder(halfExtents, start, end, cyl);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    for (const WorldSphere& sph : world.spheres) {
        const HitResult k_hr = sweepAABBvsSphere(halfExtents, start, end, sph);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    for (const WorldTriMesh& tm : world.triMeshes) {
        const HitResult k_hr = sweepAABBvsTriMesh(halfExtents, start, end, tm);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    return best;
}

HitResult sweepAll(CapsuleShape capsule, glm::vec3 start, glm::vec3 end, const WorldGeometry& world)
{
    HitResult best = sweepCapsuleVsPlanes(capsule, start, end, world.planes);

    for (const WorldAABB& box : world.boxes) {
        const HitResult k_hr = sweepCapsuleVsBox(capsule, start, end, box);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    for (const WorldBrush& brush : world.brushes) {
        const HitResult k_hr = sweepCapsuleVsBrush(capsule, start, end, brush);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    for (const WorldCylinder& cyl : world.cylinders) {
        const HitResult k_hr = sweepCapsuleVsCylinder(capsule, start, end, cyl);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    for (const WorldSphere& sph : world.spheres) {
        const HitResult k_hr = sweepCapsuleVsSphere(capsule, start, end, sph);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    for (const WorldTriMesh& tm : world.triMeshes) {
        const HitResult k_hr = sweepCapsuleVsTriMesh(capsule, start, end, tm);
        if (k_hr.hit && k_hr.tFirst < best.tFirst)
            best = k_hr;
    }

    return best;
}

// Capsule clearance queries (Phase C clearance-CA)

ClearanceResult clearanceCapsuleVsPlanes(CapsuleShape capsule, glm::vec3 pos, std::span<const Plane> planes)
{
    ClearanceResult best;
    for (const Plane& plane : planes) {
        const float s = glm::dot(plane.normal, pos) - plane.distance;
        const float r = capsule.minkowskiExtent(plane.normal);
        const float clearance = s - r;
        if (clearance < best.distance) {
            best.distance = clearance;
            best.normal = plane.normal;
            best.pointOnGeometry = pos - plane.normal * s; // foot of perpendicular
            best.surfaceType = plane.surfaceType;
        }
    }
    best.contact = best.distance < k_contactEpsilon;
    return best;
}

ClearanceResult clearanceCapsuleVsBox(CapsuleShape capsule, glm::vec3 pos, const WorldAABB& box)
{
    // Conservative: capsule's enclosing AABB vs the world AABB.  Exact would
    // require segment-vs-AABB closest-point (~30 LoC); the conservative
    // version over-approximates the capsule at corner curvature only,
    // shortening the reported clearance there — CA simply takes more
    // iterations to wrap a corner, but never penetrates.
    const glm::vec3 he = capsule.enclosingHalfExtents();
    const glm::vec3 capMin = pos - he;
    const glm::vec3 capMax = pos + he;

    glm::vec3 gapVec{0.0f};
    for (int axis = 0; axis < 3; ++axis) {
        if (capMax[axis] < box.min[axis])
            gapVec[axis] = box.min[axis] - capMax[axis];
        else if (capMin[axis] > box.max[axis])
            gapVec[axis] = -(capMin[axis] - box.max[axis]);
        // else axis-overlap, gapVec[axis] = 0
    }
    const float gapLen = glm::length(gapVec);

    ClearanceResult clr;
    clr.surfaceType = box.surfaceType;

    if (gapLen > 1e-6f) {
        // Outside box.  Normal points from box surface toward capsule centre.
        clr.distance = gapLen;
        clr.normal = -gapVec / gapLen; // gapVec is from cap to box; normal is reverse
        clr.pointOnGeometry = glm::clamp(pos, box.min, box.max);
    } else {
        // Penetrating: least-pen face MTV, exactly as `depenetrateBox`.
        const glm::vec3 expMin = box.min - he;
        const glm::vec3 expMax = box.max + he;
        float minPen = 1e30f;
        glm::vec3 pushDir{0.0f, 1.0f, 0.0f};
        const struct
        {
            float pen;
            glm::vec3 dir;
        } faces[] = {
            {pos.x - expMin.x, {-1, 0, 0}},
            {expMax.x - pos.x, {1, 0, 0}},
            {pos.y - expMin.y, {0, -1, 0}},
            {expMax.y - pos.y, {0, 1, 0}},
            {pos.z - expMin.z, {0, 0, -1}},
            {expMax.z - pos.z, {0, 0, 1}},
        };
        for (const auto& f : faces) {
            if (f.pen < minPen) {
                minPen = f.pen;
                pushDir = f.dir;
            }
        }
        clr.distance = -minPen; // negative — penetrating
        clr.normal = pushDir;
        clr.pointOnGeometry = glm::clamp(pos, box.min, box.max);
    }
    clr.contact = clr.distance < k_contactEpsilon;
    return clr;
}

ClearanceResult clearanceCapsuleVsBrush(CapsuleShape capsule, glm::vec3 pos, const WorldBrush& brush)
{
    // Plane-by-plane.  If shape is outside ANY plane, the brush is at
    // least that-plane's clearance away → take MAX of positive clearances.
    // If shape is inside ALL planes, it's penetrating; take MAX of negative
    // (least negative = closest exit plane).
    float maxPos = -1e30f;
    int maxPosPlane = -1;
    float maxNeg = -1e30f; // closest to zero from below
    int maxNegPlane = -1;

    for (int i = 0; i < brush.planeCount; ++i) {
        const Plane& p = brush.planes[i];
        const float r = capsule.minkowskiExtent(p.normal);
        const float s = glm::dot(p.normal, pos) - p.distance;
        const float clearance = s - r;
        if (clearance > 0.0f) {
            if (clearance > maxPos) {
                maxPos = clearance;
                maxPosPlane = i;
            }
        } else {
            if (clearance > maxNeg) {
                maxNeg = clearance;
                maxNegPlane = i;
            }
        }
    }

    ClearanceResult clr;
    int chosenPlane = (maxPosPlane >= 0) ? maxPosPlane : maxNegPlane;
    if (chosenPlane < 0)
        return clr; // empty brush — return default (distance=1e30)

    const Plane& p = brush.planes[chosenPlane];
    clr.distance = (maxPosPlane >= 0) ? maxPos : maxNeg;
    clr.normal = p.normal;
    clr.pointOnGeometry = pos - p.normal * (glm::dot(p.normal, pos) - p.distance);
    clr.surfaceType = p.surfaceType;
    clr.contact = clr.distance < k_contactEpsilon;
    return clr;
}

ClearanceResult clearanceCapsuleVsCylinder(CapsuleShape capsule, glm::vec3 pos, const WorldCylinder& cyl)
{
    // Conservative via the cylinder's enclosing AABB.  Cylinder side
    // curvature is the only thing this approximation loses, and only at
    // the rim; CA's iterative nature handles the resulting longer
    // wrap-around in extra iterations.
    WorldAABB cylAABB;
    cylAABB.min = glm::vec3(cyl.base.x - cyl.radius, cyl.base.y, cyl.base.z - cyl.radius);
    cylAABB.max = glm::vec3(cyl.base.x + cyl.radius, cyl.base.y + cyl.height, cyl.base.z + cyl.radius);
    cylAABB.surfaceType = cyl.surfaceType;
    return clearanceCapsuleVsBox(capsule, pos, cylAABB);
}

ClearanceResult clearanceCapsuleVsSphere(CapsuleShape capsule, glm::vec3 pos, const WorldSphere& sph)
{
    // Exact: closest point on capsule axis (segment) to sphere centre.
    const glm::vec3 segA = capsule.segA(pos);
    const glm::vec3 segB = capsule.segB(pos);
    const glm::vec3 closestAxis = closestPointSegmentPoint(segA, segB, sph.center);
    const glm::vec3 fromSphereToAxis = closestAxis - sph.center;
    const float axisDist = glm::length(fromSphereToAxis);

    ClearanceResult clr;
    clr.distance = axisDist - capsule.radius - sph.radius;
    if (axisDist > 1e-6f) {
        clr.normal = fromSphereToAxis / axisDist;
        clr.pointOnGeometry = sph.center + clr.normal * sph.radius;
    } else {
        // Capsule axis coincides with sphere centre — degenerate.
        clr.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        clr.pointOnGeometry = sph.center + clr.normal * sph.radius;
    }
    clr.surfaceType = sph.surfaceType;
    clr.contact = clr.distance < k_contactEpsilon;
    return clr;
}

ClearanceResult clearanceCapsuleVsWorld(CapsuleShape capsule, glm::vec3 pos, const WorldGeometry& world)
{
    ClearanceResult best;
    auto consider = [&](const ClearanceResult& c) {
        if (c.distance < best.distance)
            best = c;
    };

    consider(clearanceCapsuleVsPlanes(capsule, pos, world.planes));
    for (const WorldAABB& box : world.boxes)
        consider(clearanceCapsuleVsBox(capsule, pos, box));
    for (const WorldBrush& brush : world.brushes)
        consider(clearanceCapsuleVsBrush(capsule, pos, brush));
    for (const WorldCylinder& cyl : world.cylinders)
        consider(clearanceCapsuleVsCylinder(capsule, pos, cyl));
    for (const WorldSphere& sph : world.spheres)
        consider(clearanceCapsuleVsSphere(capsule, pos, sph));

    // Trimesh search radius: at least capsule.radius (to find any contact),
    // bounded by current best clearance plus margin (BVH cull won't process
    // triangles further than this).  If best is still huge (1e30) — no
    // convex primitive is close — fall back to a generous 1024 u search.
    const float meshSearchRadius =
        (best.distance < 1024.0f) ? (best.distance + capsule.radius + 16.0f) : 1024.0f;
    for (const WorldTriMesh& tm : world.triMeshes)
        consider(clearanceCapsuleVsTriMesh(capsule, pos, meshSearchRadius, tm));

    best.contact = best.distance < k_contactEpsilon;
    return best;
}

// sphereCast
//
// Expands each piece of geometry by the sphere radius (Minkowski sum), then
// tests the sphere centre as a point/ray against the expanded geometry.
// This gives exact results for planes and brushes, and slightly conservative
// results for AABB corners (inflated box instead of rounded box), which is
// acceptable and even desirable for wall detection generosity.

SphereHitResult sphereCast(float radius, glm::vec3 start, glm::vec3 end, const WorldGeometry& world)
{
    SphereHitResult best;
    const glm::vec3 k_delta = end - start;

    // Test against infinite planes
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

    // Test against AABBs (inflated by sphere radius)
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

    // Test against brushes (each plane expanded by radius)
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

    // Test against cylinders (expanded by sphere radius)
    for (const WorldCylinder& cyl : world.cylinders) {
        const float k_effR = cyl.radius + radius;
        const float k_yMin = cyl.base.y - radius;
        const float k_yMax = cyl.base.y + cyl.height + radius;

        // Y slab
        float tYentry = -1e30f, tYexit = 1e30f;
        bool yCapBottom = false;
        if (std::abs(k_delta.y) < 1e-8f) {
            if (start.y < k_yMin || start.y > k_yMax)
                continue;
        } else {
            float t1 = (k_yMin - start.y) / k_delta.y;
            float t2 = (k_yMax - start.y) / k_delta.y;
            bool t1bot = true;
            if (t1 > t2) {
                std::swap(t1, t2);
                t1bot = false;
            }
            tYentry = t1;
            tYexit = t2;
            yCapBottom = t1bot;
        }

        // XZ circle
        const float ox = start.x - cyl.base.x, oz = start.z - cyl.base.z;
        const float dx = k_delta.x, dz = k_delta.z;
        const float a = dx * dx + dz * dz;
        const float b = 2.0f * (ox * dx + oz * dz);
        const float c = ox * ox + oz * oz - k_effR * k_effR;
        float tXZentry = -1e30f, tXZexit = 1e30f;
        if (a < 1e-12f) {
            if (c > 0.0f)
                continue;
        } else {
            const float disc = b * b - 4.0f * a * c;
            if (disc < 0.0f)
                continue;
            const float sq = std::sqrt(disc);
            tXZentry = (-b - sq) / (2.0f * a);
            tXZexit = (-b + sq) / (2.0f * a);
        }

        bool isYcap = tYentry > tXZentry;
        float tE = isYcap ? tYentry : tXZentry;
        float tX = std::min(tYexit, tXZexit);
        if (tE > tX || tX < 0.0f || tE < 0.0f || tE >= best.t)
            continue;

        glm::vec3 n;
        if (isYcap) {
            n = yCapBottom ? glm::vec3(0, -1, 0) : glm::vec3(0, 1, 0);
        } else {
            glm::vec3 hp = start + k_delta * tE;
            n = glm::vec3(hp.x - cyl.base.x, 0, hp.z - cyl.base.z);
            float ln = glm::length(n);
            n = ln > 1e-6f ? n / ln : glm::vec3(1, 0, 0);
        }
        best.hit = true;
        best.t = tE;
        best.normal = n;
        best.point = start + k_delta * tE - n * radius;
    }

    // Test against world spheres (expanded by cast radius)
    for (const WorldSphere& ws : world.spheres) {
        const float k_effR = ws.radius + radius;
        const glm::vec3 oc = start - ws.center;
        const float a = glm::dot(k_delta, k_delta);
        if (a < 1e-12f)
            continue;
        const float b = 2.0f * glm::dot(oc, k_delta);
        const float c = glm::dot(oc, oc) - k_effR * k_effR;
        if (c <= 0.0f)
            continue; // inside
        const float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f)
            continue;
        const float t = (-b - std::sqrt(disc)) / (2.0f * a);
        if (t < 0.0f || t >= best.t)
            continue;
        glm::vec3 hp = start + k_delta * t;
        glm::vec3 n = hp - ws.center;
        float ln = glm::length(n);
        n = ln > 1e-6f ? n / ln : glm::vec3(0, 1, 0);
        best.hit = true;
        best.t = t;
        best.normal = n;
        best.point = hp - n * radius;
    }

    // Test against triangle meshes (conservative: treat sphere as AABB)
    for (const WorldTriMesh& tm : world.triMeshes) {
        const glm::vec3 sphereHalf{radius, radius, radius};
        const HitResult hr = sweepAABBvsTriMesh(sphereHalf, start, end, tm);
        if (hr.hit && hr.tFirst < best.t) {
            best.hit = true;
            best.t = hr.tFirst;
            best.normal = hr.normal;
            best.point = start + k_delta * hr.tFirst - hr.normal * radius;
        }
    }

    return best;
}

} // namespace physics
