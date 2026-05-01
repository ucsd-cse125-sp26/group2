/// @file EntityInterpolation.cpp
/// @brief Implementation of append + bracket-search sampling for InterpolationBuffer.

#include "EntityInterpolation.hpp"

#include "ecs/components/InterpolationBuffer.hpp"

#include <cstddef>
#include <entt/entt.hpp>
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

namespace entity_interpolation
{

void appendSample(entt::registry& registry, entt::entity e, Uint64 captureNs, const glm::vec3& position, float yaw)
{
    auto& buf = registry.get_or_emplace<InterpolationBuffer>(e);

    // Drop duplicate-timestamp samples — possible if the server ships a
    // FULL right after a DELTA in the same SDL_GetTicksNS quantum (rare,
    // but the lookup's `(b.captureNs - a.captureNs)` would divide by 0).
    // We keep the *newer* values — they reflect the latest snapshot.
    if (buf.count > 0) {
        const std::size_t lastIdx = (buf.head + InterpolationBuffer::k_capacity - 1) % InterpolationBuffer::k_capacity;
        if (buf.ring[lastIdx].captureNs == captureNs) {
            buf.ring[lastIdx].position = position;
            buf.ring[lastIdx].yaw = yaw;
            return;
        }
    }

    auto& slot = buf.ring[buf.head];
    slot.captureNs = captureNs;
    slot.position = position;
    slot.yaw = yaw;
    buf.head = (buf.head + 1) % InterpolationBuffer::k_capacity;
    if (buf.count < InterpolationBuffer::k_capacity)
        ++buf.count;
}

namespace
{

/// @brief Linear interpolation along the shortest arc between two yaw
/// angles in radians. Without the wrap fix-up, lerping from 3.0 rad to
/// -3.0 rad (just past ±π) would sweep the long way around through 0,
/// snapping the player's character mid-frame.
float lerpYaw(float a, float b, float t) noexcept
{
    constexpr auto pi = glm::pi<float>();
    constexpr float twoPi = 2.0f * pi;
    float diff = b - a;
    while (diff > pi)
        diff -= twoPi;
    while (diff < -pi)
        diff += twoPi;
    return a + diff * t;
}

} // namespace

InterpolatedTransform sample(const entt::registry& registry,
                             entt::entity e,
                             Uint64 renderTimeNs,
                             const glm::vec3& fallbackPos,
                             float fallbackYaw)
{
    InterpolatedTransform out{.position = fallbackPos, .yaw = fallbackYaw, .fromBuffer = false};

    const auto* buf = registry.try_get<InterpolationBuffer>(e);
    if (buf == nullptr || buf->count < 2 || renderTimeNs == 0)
        return out;

    constexpr std::size_t cap = InterpolationBuffer::k_capacity;
    const std::size_t n = buf->count;

    // The ring stores entries in chronological order if walked from
    // (head - count) up to (head - 1), all mod capacity.  Compute the
    // physical index for chronological position `i ∈ [0, n)`.
    auto chronoIdx = [&](std::size_t i) -> std::size_t { return (buf->head + cap - n + i) % cap; };

    const auto& oldest = buf->ring[chronoIdx(0)];
    const auto& newest = buf->ring[chronoIdx(n - 1)];

    // Outside the buffered window — snap to ends.  No extrapolation past
    // the newest sample (Source-engine policy: a frozen entity is less
    // visually offensive than an overshooting one).
    if (renderTimeNs <= oldest.captureNs) {
        out.position = oldest.position;
        out.yaw = oldest.yaw;
        out.fromBuffer = true;
        return out;
    }
    if (renderTimeNs >= newest.captureNs) {
        out.position = newest.position;
        out.yaw = newest.yaw;
        out.fromBuffer = true;
        return out;
    }

    // Linear scan for the bracketing pair.  n ≤ k_capacity (8) so this is
    // ~7 compares worst-case — branch-predictable, stays in cache, no
    // need for a binary search.
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const auto& a = buf->ring[chronoIdx(i)];
        const auto& b = buf->ring[chronoIdx(i + 1)];
        if (renderTimeNs >= a.captureNs && renderTimeNs < b.captureNs) {
            const Uint64 span = b.captureNs - a.captureNs;
            if (span == 0) {
                // Degenerate (caller violated monotonic-timestamps invariant);
                // bias to newer to keep motion forward-progressing.
                out.position = b.position;
                out.yaw = b.yaw;
                out.fromBuffer = true;
                return out;
            }
            const float t = static_cast<float>(renderTimeNs - a.captureNs) / static_cast<float>(span);
            out.position = glm::mix(a.position, b.position, t);
            out.yaw = lerpYaw(a.yaw, b.yaw, t);
            out.fromBuffer = true;
            return out;
        }
    }

    // Shouldn't reach: oldest/newest bracket the timestamp by the early
    // checks above. Fall back to newest for safety.
    out.position = newest.position;
    out.yaw = newest.yaw;
    out.fromBuffer = true;
    return out;
}

} // namespace entity_interpolation
