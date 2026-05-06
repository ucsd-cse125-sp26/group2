/// @file InterpolationBuffer.hpp
/// @brief Per-entity ring of recent snapshot samples for renderer interpolation.

#pragma once

#include "AnimSnapshot.hpp"

#include <SDL3/SDL_stdinc.h>

#include <array>
#include <cstddef>
#include <glm/vec3.hpp>

/// @brief Render-time interpolation history for a remote entity.
///
/// PR-11 (server-perf): entity interpolation with N-tick render delay
/// (Valorant / Fortnite / Source-engine `cl_interp` style).  Each time a
/// snapshot is applied, the network thread appends one Sample per
/// replicated entity into its ring.  At render time, the renderer asks for
/// the entity's state at `now - (delayTicks × snapshotInterval)` — the
/// lookup walks the ring for the two samples bracketing that timestamp
/// and lerps between them.
///
/// Why a ring?  To absorb network jitter and packet loss.  A 2-tick
/// render delay (~62.5 ms at 32 Hz snapshot rate) means the renderer
/// always has ≥ 1 future sample to interpolate toward, so a single
/// dropped snapshot is invisible — the lerp targets the next-arriving
/// sample instead.  Without the buffer (Phase 5a), a missed snapshot
/// froze the entity for ~31 ms (alpha clamps to 1.0 in the old path).
///
/// Capacity 8 covers ~250 ms of history at 32 Hz — enough to smooth any
/// reasonable network jitter while staying tiny: 8 × (8 + 12 + 4) bytes
/// = 192 B / entity, ≈ 96 KB at 500 entities.
///
/// @note Client-only.  The local player is *excluded* — that entity uses
///       client-side prediction and renders at "now", not "now - delay".
///       Excluded by checking `LocalPlayer` before append.
struct InterpolationBuffer
{
    /// @brief Maximum samples retained.  Sized for ~250 ms at 32 Hz snapshot rate.
    static constexpr std::size_t k_capacity = 8;

    /// @brief One snapshot's worth of interpolatable state.
    ///
    /// PR-28: extended beyond `(position, yaw)` to also carry the inputs
    /// the client's `CharacterAnimator` needs each frame.  Pre-PR-28 the
    /// renderer interp-delayed the entity's POSITION via this buffer
    /// while the animator continued to read the LATEST-snapshot Velocity
    /// + pitch + PlayerVisState bits — so the animator was poseing at
    /// "now" while the body was rendered at "now − cl_interp".  At
    /// 30+ ms render delay the running-vs-idle, walk-vs-sprint, and
    /// air-vs-ground flags would flip ahead of the visible motion,
    /// producing the 0.41-median anim-state delta the PR-27a
    /// telemetry caught.  Storing the animator inputs here and
    /// sampling them at the same render time aligns body and pose.
    struct Sample
    {
        Uint64 captureNs = 0;     ///< SDL_GetTicksNS() at append time.
        glm::vec3 position{0.0f}; ///< World-space position.
        glm::vec3 velocity{0.0f}; ///< World-space velocity (PR-28).
        float yaw = 0.0f;         ///< Player yaw (rad).  0 for non-player entities.
        float pitch = 0.0f;       ///< Player pitch (rad, PR-28).

        // PlayerVisState fields the animator reads, interp-delayed
        // alongside the position so locomotion-state transitions
        // happen at the SAME logical instant the body visibly
        // changes pose.  Stored as `uint8_t` casts to keep this
        // header free of `PlayerStateEnums.hpp` dependency.
        std::uint8_t moveMode = 0;    ///< MoveMode enum cast (PR-28).
        std::uint8_t wallRunSide = 0; ///< WallSide enum cast (PR-28).
        bool grounded = false;        ///< PR-28.
        bool sprinting = false;       ///< PR-28.
        bool crouching = false;       ///< PR-28.

        /// @brief PR-29: server-authoritative animation state at this
        /// sample's tick.  Replicated via the Synced tuple, captured
        /// here per-snapshot so the renderer can render the body's
        /// interp-delayed pose with `CharacterAnimator::
        /// renderFromServer(buffered.anim, …)` instead of letting the
        /// client's animator free-run and accumulate timeRatio drift
        /// against the server's authoritative animator.
        AnimSnapshot anim{};
    };

    std::array<Sample, k_capacity> ring{};
    std::size_t head = 0;  ///< Next write index (mod k_capacity).
    std::size_t count = 0; ///< Live entries; saturates at k_capacity.
};
