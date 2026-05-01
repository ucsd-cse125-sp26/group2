/// @file InterpolationBuffer.hpp
/// @brief Per-entity ring of recent snapshot samples for renderer interpolation.

#pragma once

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
    struct Sample
    {
        Uint64 captureNs = 0;     ///< SDL_GetTicksNS() at append time.
        glm::vec3 position{0.0f}; ///< World-space position.
        float yaw = 0.0f;         ///< Player yaw (rad).  0 for non-player entities.
    };

    std::array<Sample, k_capacity> ring{};
    std::size_t head = 0;  ///< Next write index (mod k_capacity).
    std::size_t count = 0; ///< Live entries; saturates at k_capacity.
};
