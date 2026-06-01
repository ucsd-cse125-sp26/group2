#pragma once

#include <algorithm>
#include <cstdint>

/// Pure (dependency-free) lag-compensation rewind math.  Lives in its own
/// header so it can be unit-tested in isolation — `ServerGame` includes it
/// from the hot path in `updateLagCompTargets`.
namespace server::lagcomp
{

/// Round a millisecond duration to the nearest whole physics tick.
/// Integer-only round-to-nearest via `(ms * Hz + 500) / 1000`.
///
/// @note `ms` is taken as uint32 so the multiply can't overflow for any
///       uint16 wire value (65535 × 1000 Hz still fits uint32).
inline constexpr std::uint32_t msToTicks(std::uint32_t ms, std::uint32_t tickRateHz)
{
    return (ms * tickRateHz + 500u) / 1000u;
}

/// Compute the lag-compensation rewind depth (in physics ticks) for a
/// shooter from their last-reported net state.
///
/// PR-31: prefer the client's *measured* render delay in milliseconds
/// (`interpDelayMs`, derived client-side from an EMA of observed
/// snapshot-apply intervals) whenever it is non-zero.  The measured value
/// tracks the real snapshot cadence — including the drift introduced by a
/// highly jittery link and the post-join warm-up window before the EMA
/// converges — so the server rewinds to exactly the render time the client
/// actually displayed when it fired.
///
/// Falls back to the legacy snapshot-count estimate
/// (`interpDelaySnapshots × snapshotEveryNTicks`) when `interpDelayMs == 0`,
/// i.e. render-delay interpolation is disabled (`cl_interp 0`) or the client
/// predates the ms field.  That path uses the server's *nominal* fixed
/// cadence, which can diverge from the client's true render delay under
/// jitter — exactly the mismatch the ms field eliminates.
///
/// The combined RTT + interp depth is clamped to `maxLagCompTicks` so the
/// rewind never exceeds the HitboxHistory ring.
///
/// @param rttMs                Client's smoothed full round-trip time (ms).
/// @param interpDelayMs        Client's measured render delay (ms); 0 selects fallback.
/// @param interpDelaySnapshots Client's render delay in snapshots (fallback term).
/// @param snapshotEveryNTicks  Server's nominal physics-ticks-per-snapshot.
/// @param tickRateHz           Physics tick rate.
/// @param maxLagCompTicks      Upper clamp (= HitboxHistory ring depth).
inline std::uint32_t computeRewindTicks(std::uint16_t rttMs,
                                        std::uint16_t interpDelayMs,
                                        std::uint8_t interpDelaySnapshots,
                                        std::uint32_t snapshotEveryNTicks,
                                        std::uint32_t tickRateHz,
                                        std::uint32_t maxLagCompTicks)
{
    // PR-20.7: full-RTT (NOT half-RTT).  Our client renders remote players
    // at `most_recent_snapshot_apply − cl_interp` rather than predicting
    // them forward to estimated server-now, so the rewind has to absorb
    // both the inbound and outbound legs of the RTT.  See
    // ServerGame::updateLagCompTargets for the full derivation.
    const std::uint32_t rttTicks = msToTicks(rttMs, tickRateHz);

    const std::uint32_t interpDelayTicks = (interpDelayMs > 0) ? msToTicks(interpDelayMs, tickRateHz)
                                                               : static_cast<std::uint32_t>(interpDelaySnapshots) *
                                                                     std::max<std::uint32_t>(1u, snapshotEveryNTicks);

    return std::min<std::uint32_t>(rttTicks + interpDelayTicks, maxLagCompTicks);
}

} // namespace server::lagcomp
