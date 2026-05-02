/// @file EntityInterpolation.hpp
/// @brief Append + lookup helpers for the per-entity InterpolationBuffer.
///
/// PR-11 (server-perf): client-side render-delay interpolation.  Network
/// thread calls `appendSample` after each snapshot apply; the renderer
/// calls `sample` to read back the (position, yaw) the entity should be
/// drawn at for the current frame's "delayed-now" timestamp.
///
/// The two helpers are decoupled from `Client` so they can be unit-tested
/// in isolation and reused if other places (e.g. replay scrubber) want
/// to drive interpolation timestamps manually.

#pragma once

#include "ecs/components/InterpolationBuffer.hpp" // NOLINT(misc-include-cleaner) — surfaced as part of the API contract for debug-UI consumers.

#include <SDL3/SDL_stdinc.h>

#include <entt/entity/fwd.hpp>
#include <glm/vec3.hpp>

namespace entity_interpolation
{

/// @brief Result of sampling an entity's interpolation buffer.
///
/// PR-28 extended the result with the animator-input fields the
/// renderer also needs at the same render time.  Continuous fields
/// (position, velocity, yaw, pitch) are LERPed between bracketing
/// samples; discrete fields (`moveMode`, `wallRunSide`, `grounded`,
/// `sprinting`, `crouching`) take the value of the older bracketing
/// sample (no fractional state-machine state — pose transitions
/// snap at the same instant the interpolation crosses the sample).
struct InterpolatedTransform
{
    glm::vec3 position{0.0f}; ///< Interpolated (or fallback) position.
    glm::vec3 velocity{0.0f}; ///< Interpolated (or fallback) velocity (PR-28).
    float yaw = 0.0f;         ///< Interpolated (or fallback) yaw.
    float pitch = 0.0f;       ///< Interpolated (or fallback) pitch (PR-28).

    // Discrete animator inputs — older-bracketing-sample, not lerped.
    std::uint8_t moveMode = 0;    ///< MoveMode enum cast (PR-28).
    std::uint8_t wallRunSide = 0; ///< WallSide enum cast (PR-28).
    bool grounded = false;        ///< PR-28.
    bool sprinting = false;       ///< PR-28.
    bool crouching = false;       ///< PR-28.

    bool fromBuffer = false;      ///< True if a real lerp happened; false on fallback.
};

/// @brief PR-28: full sample payload.  `appendSample` takes one of these
/// per snapshot apply.
struct SampleInputs
{
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::uint8_t moveMode = 0;
    std::uint8_t wallRunSide = 0;
    bool grounded = false;
    bool sprinting = false;
    bool crouching = false;
};

/// @brief Append a snapshot sample at @p captureNs into the entity's
/// `InterpolationBuffer`, creating the component on first call.
///
/// O(1) — single ring slot write.  Caller is responsible for excluding
/// the local player (whose render path uses prediction, not interpolation).
///
/// @param registry  Client registry.
/// @param e         Entity that just had a snapshot applied to it.
/// @param captureNs Wall-clock timestamp (SDL_GetTicksNS()) of the apply.
///                  Must be monotonically non-decreasing per-entity for
///                  the bracket-search in `sample` to work correctly; in
///                  practice callers pass the same `now` for every
///                  entity they touch in a single dispatchMessage.
/// @param inputs    Full sample payload — position + velocity + yaw +
///                  pitch + animator-relevant PlayerVisState bits.
void appendSample(entt::registry& registry, entt::entity e, Uint64 captureNs, const SampleInputs& inputs);

/// @brief Look up @p e's interpolated transform at @p renderTimeNs.
///
/// Walks the entity's `InterpolationBuffer` for the two adjacent samples
/// bracketing @p renderTimeNs and lerps between them.  Behaviour at the
/// edges (mirrors Source engine and Phase 5a's freeze-no-extrapolate
/// policy):
///   - No buffer / < 2 samples / `renderTimeNs == 0` → returns
///     `{fallbackPos, fallbackYaw, fromBuffer = false}` (other fields zero).
///   - `renderTimeNs ≤ oldest` → returns oldest sample (snap to start).
///   - `renderTimeNs ≥ newest` → returns newest sample (freeze; no
///     extrapolation).
///   - Otherwise → linear interpolation between bracketing samples for
///     continuous fields; older sample for discrete fields, `fromBuffer
///     = true`.
///
/// O(8) worst case — capacity is fixed at `InterpolationBuffer::k_capacity`.
InterpolatedTransform sample(const entt::registry& registry,
                             entt::entity e,
                             Uint64 renderTimeNs,
                             const glm::vec3& fallbackPos,
                             float fallbackYaw);

} // namespace entity_interpolation
