/// @file SimdAabb.hpp
/// @brief SIMD AABB-vs-AABB and ray-vs-AABB batch helpers.
///
/// 4-wide batched tests for the broadphase / trimesh BVH inner loop.
/// Falls back to scalar code on non-x86 platforms so the API is portable.
///
/// **Determinism caveat.** SSE/AVX horizontal reductions (`hadd_ps`,
/// `_mm_movemask_ps`) compute the same numeric result as scalar code
/// for these tests because we only check signs / mask bits — no float
/// reductions across lanes.  Output is bit-equal across SSE / scalar
/// builds.

#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP)
#define GROUP2_HAVE_SSE2 1
#include <emmintrin.h>
#else
#define GROUP2_HAVE_SSE2 0
#endif

namespace physics::simd
{

/// @brief Test 4 candidate AABBs against one query AABB.  Returns a
/// 4-bit mask: bit i = 1 iff candidate i overlaps the query.
///
/// AoSoA layout — caller packs candidates into 6 SoA arrays
/// (minX/Y/Z, maxX/Y/Z) of 4 floats each.  Suits the broadphase tree's
/// "test 4 nodes against one query" pattern.
inline uint32_t aabbBatchOverlap(const float (&minX)[4],
                                 const float (&minY)[4],
                                 const float (&minZ)[4],
                                 const float (&maxX)[4],
                                 const float (&maxY)[4],
                                 const float (&maxZ)[4],
                                 glm::vec3 queryMin,
                                 glm::vec3 queryMax) noexcept
{
#if GROUP2_HAVE_SSE2
    const __m128 qMinX = _mm_set1_ps(queryMin.x);
    const __m128 qMinY = _mm_set1_ps(queryMin.y);
    const __m128 qMinZ = _mm_set1_ps(queryMin.z);
    const __m128 qMaxX = _mm_set1_ps(queryMax.x);
    const __m128 qMaxY = _mm_set1_ps(queryMax.y);
    const __m128 qMaxZ = _mm_set1_ps(queryMax.z);

    const __m128 cMinX = _mm_loadu_ps(minX);
    const __m128 cMinY = _mm_loadu_ps(minY);
    const __m128 cMinZ = _mm_loadu_ps(minZ);
    const __m128 cMaxX = _mm_loadu_ps(maxX);
    const __m128 cMaxY = _mm_loadu_ps(maxY);
    const __m128 cMaxZ = _mm_loadu_ps(maxZ);

    // Overlap on axis a iff cMin[a] <= qMax[a] AND cMax[a] >= qMin[a].
    const __m128 xOk = _mm_and_ps(_mm_cmple_ps(cMinX, qMaxX), _mm_cmpge_ps(cMaxX, qMinX));
    const __m128 yOk = _mm_and_ps(_mm_cmple_ps(cMinY, qMaxY), _mm_cmpge_ps(cMaxY, qMinY));
    const __m128 zOk = _mm_and_ps(_mm_cmple_ps(cMinZ, qMaxZ), _mm_cmpge_ps(cMaxZ, qMinZ));

    const __m128 all = _mm_and_ps(_mm_and_ps(xOk, yOk), zOk);
    return static_cast<uint32_t>(_mm_movemask_ps(all));
#else
    uint32_t mask = 0;
    for (int i = 0; i < 4; ++i) {
        const bool ok = minX[i] <= queryMax.x && maxX[i] >= queryMin.x && minY[i] <= queryMax.y &&
                        maxY[i] >= queryMin.y && minZ[i] <= queryMax.z && maxZ[i] >= queryMin.z;
        if (ok)
            mask |= (1u << i);
    }
    return mask;
#endif
}

} // namespace physics::simd
