/// @file ShotLog.hpp
/// @brief PR-18b — server-side shot-resolution log for the netsync framework.
///
/// Each call to `recordShotResolution` appends one CSV row with the
/// shot's authoritative outcome (shooter, the input tick the client
/// stamped on the firing input, hit target if any, hit point in
/// world space).  The companion offline analyzer
/// (`scripts/netsync-analyze.py --shots`) joins this with the
/// existing bot-side observation log to compute hit-rate vs network
/// conditions and surface lag-comp regressions deterministically.
///
/// Keyed by `(shooterClientId, shotInputTick)` — the same pair the
/// client used to stamp the input — so future bot-side shot-intent
/// logs (PR-18c) can match without any extra wire-format changes.
///
/// File path comes from `GROUP2_SERVER_SHOTS_CSV`.  No-op when env
/// var unset; load tests stay free of disk I/O cost.

#pragma once

#include <cstdint>

namespace group2::perf::shotlog
{

/// @brief Sentinel `hitClientId` value meaning "shot missed all
/// targets".  We use 0xFFFF instead of 0 because ClientId 0 is a
/// legitimate connected client (the first one to join).
inline constexpr std::uint16_t k_missClientId = 0xFFFFu;

/// @brief Open the log file from `GROUP2_SERVER_SHOTS_CSV` if set,
/// write the CSV header, and remember the FILE* for subsequent
/// `recordShotResolution` calls.  Idempotent — second call is a
/// no-op so callers don't need to gate on first-init.
void openIfRequested();

/// @brief Per-shot record for the server-side shot-resolution log.
/// PR-22 (netsync): grew from a flat parameter list to a POD so the
/// callsite can fill the rewind-related fields incrementally inside
/// the `RewindHitboxesGuard` scope without pushing 14 arguments
/// through `recordShotResolution`.  All fields are zeroed by default
/// — the log writes them verbatim, so a missed shot or a non-rewound
/// shooter just produces zero columns.
struct ShotResolution
{
    std::uint16_t shooterClientId = 0;
    std::uint32_t shotInputTick = 0;
    std::uint16_t hitClientId = k_missClientId;
    float hitX = 0.0f;
    float hitY = 0.0f;
    float hitZ = 0.0f;
    int hitRegion = 0;

    // PR-22: shot ray (server's view).
    float originX = 0.0f;
    float originY = 0.0f;
    float originZ = 0.0f;
    float dirX = 0.0f;
    float dirY = 0.0f;
    float dirZ = 0.0f;

    // PR-22: lag-comp diagnostics — read off the shooter's
    // `LagCompTarget` component so the analyzer can bucket shots by
    // RTT and rewind size without joining against per-client net
    // stats.
    std::uint16_t shooterRttMs = 0;
    std::uint16_t lagCompTicks = 0;

    // PR-22: hit target rewound vs current centre.  When the shooter
    // had a non-zero `LagCompTarget`, the rewinder swapped the hit
    // target's capsules to the historical sample; the centroid of
    // those capsules at log-time is `(rewoundX, rewoundY, rewoundZ)`.
    // The current centre is `Position.value` of the same entity (the
    // rewinder doesn't touch `Position`).  Diff between the two
    // numbers = how far the lag-comp moved the target backwards in
    // time.  All zeros on miss.
    float hitTargetRewoundX = 0.0f;
    float hitTargetRewoundY = 0.0f;
    float hitTargetRewoundZ = 0.0f;
    float hitTargetCurrentX = 0.0f;
    float hitTargetCurrentY = 0.0f;
    float hitTargetCurrentZ = 0.0f;
};

/// @brief Append one shot-resolution row.  Thread-safe — the
/// implementation guards the file with an internal mutex so multiple
/// game-thread weapon-system call sites can write without colliding.
/// No-op when the log isn't open (env var unset, or open failed).
void recordShotResolution(const ShotResolution& shot);

/// @brief Flush + close the log file.  Called from ServerGame::shutdown.
void close() noexcept;

} // namespace group2::perf::shotlog
