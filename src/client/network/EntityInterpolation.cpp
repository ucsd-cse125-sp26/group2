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

namespace
{
inline void writeSample(InterpolationBuffer::Sample& slot, Uint64 captureNs, const SampleInputs& in)
{
    slot.captureNs = captureNs;
    slot.position = in.position;
    slot.velocity = in.velocity;
    slot.yaw = in.yaw;
    slot.pitch = in.pitch;
    slot.moveMode = in.moveMode;
    slot.wallRunSide = in.wallRunSide;
    slot.grounded = in.grounded;
    slot.sprinting = in.sprinting;
    slot.crouching = in.crouching;
    slot.anim = in.anim; // PR-29: server-authoritative animation state at this tick.
}
} // namespace

void appendSample(entt::registry& registry, entt::entity e, Uint64 captureNs, const SampleInputs& inputs)
{
    auto& buf = registry.get_or_emplace<InterpolationBuffer>(e);

    // Drop duplicate-timestamp samples — possible if the server ships a
    // FULL right after a DELTA in the same SDL_GetTicksNS quantum (rare,
    // but the lookup's `(b.captureNs - a.captureNs)` would divide by 0).
    // We keep the *newer* values — they reflect the latest snapshot.
    if (buf.count > 0) {
        const std::size_t lastIdx = (buf.head + InterpolationBuffer::k_capacity - 1) % InterpolationBuffer::k_capacity;
        if (buf.ring[lastIdx].captureNs == captureNs) {
            writeSample(buf.ring[lastIdx], captureNs, inputs);
            return;
        }
    }

    writeSample(buf.ring[buf.head], captureNs, inputs);
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

namespace
{
/// @brief PR-28: copy a sample's discrete (non-lerped) fields into the
/// output.  Continuous fields are filled by the caller (lerped or
/// snap-to-end as appropriate).
inline void copyDiscreteFields(InterpolatedTransform& out, const InterpolationBuffer::Sample& s)
{
    out.moveMode = s.moveMode;
    out.wallRunSide = s.wallRunSide;
    out.grounded = s.grounded;
    out.sprinting = s.sprinting;
    out.crouching = s.crouching;
}

/// @brief PR-29: blend two animation snapshots per slot.  Per slot:
///   * different clip-id → snap to older (state transitions are discrete);
///   * same clip-id, same active-state → lerp `timeRatio` and `weight`;
///   * different active-state → snap to older (treated as transition).
/// The fold-back-to-older is what makes the discrete "started a new
/// clip" event happen at the SAME tick the body's pose-change is rendered.
inline AnimSnapshot lerpAnim(const AnimSnapshot& a, const AnimSnapshot& b, float t)
{
    AnimSnapshot out = a;
    for (std::size_t i = 0; i < AnimSnapshot::k_numSlots; ++i) {
        const auto& sa = a.slots[i];
        const auto& sb = b.slots[i];
        const bool aActive = sa.weight > 0.0f;
        const bool bActive = sb.weight > 0.0f;
        if (sa.clipIdRaw != sb.clipIdRaw || aActive != bActive) {
            out.slots[i] = sa; // discrete transition — snap to older
            continue;
        }
        if (!aActive) {
            out.slots[i] = sa; // both inactive — nothing to lerp
            continue;
        }
        out.slots[i].clipIdRaw = sa.clipIdRaw;
        out.slots[i].timeRatio = sa.timeRatio + (sb.timeRatio - sa.timeRatio) * t;
        out.slots[i].weight = sa.weight + (sb.weight - sa.weight) * t;
    }
    return out;
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
        out.velocity = oldest.velocity;
        out.yaw = oldest.yaw;
        out.pitch = oldest.pitch;
        copyDiscreteFields(out, oldest);
        out.anim = oldest.anim;
        out.fromBuffer = true;
        return out;
    }
    if (renderTimeNs >= newest.captureNs) {
        out.position = newest.position;
        out.velocity = newest.velocity;
        out.yaw = newest.yaw;
        out.pitch = newest.pitch;
        copyDiscreteFields(out, newest);
        out.anim = newest.anim;
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
                out.velocity = b.velocity;
                out.yaw = b.yaw;
                out.pitch = b.pitch;
                copyDiscreteFields(out, b);
                out.anim = b.anim;
                out.fromBuffer = true;
                return out;
            }
            const float t = static_cast<float>(renderTimeNs - a.captureNs) / static_cast<float>(span);
            out.position = glm::mix(a.position, b.position, t);
            out.velocity = glm::mix(a.velocity, b.velocity, t);
            out.yaw = lerpYaw(a.yaw, b.yaw, t);
            out.pitch = a.pitch + (b.pitch - a.pitch) * t;
            // Discrete fields take the older bracketing sample so
            // animation state-machine transitions snap at the same
            // instant the visible motion crosses the sample boundary.
            copyDiscreteFields(out, a);
            // PR-29: anim slots LERP timeRatio (per-slot, when both
            // bracketing samples have the same clip), snap discrete
            // fields to older (clip transitions match the body-pose
            // transition instant exactly).
            out.anim = lerpAnim(a.anim, b.anim, t);
            out.fromBuffer = true;
            return out;
        }
    }

    // Shouldn't reach: oldest/newest bracket the timestamp by the early
    // checks above. Fall back to newest for safety.
    out.position = newest.position;
    out.velocity = newest.velocity;
    out.yaw = newest.yaw;
    out.pitch = newest.pitch;
    copyDiscreteFields(out, newest);
    out.anim = newest.anim;
    out.fromBuffer = true;
    return out;
}

} // namespace entity_interpolation
