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

/// @brief Append one shot-resolution row.  Thread-safe — the
/// implementation guards the file with an internal mutex so multiple
/// game-thread weapon-system call sites can write without colliding.
/// No-op when the log isn't open (env var unset, or open failed).
///
/// @param shooterClientId  Server's ClientId for the firing entity.
/// @param shotInputTick    The client-stamped tick on the
///                         InputSnapshot that drove this fire.  The
///                         analyzer keys on (shooter, shotInputTick)
///                         to match against bot-side shot-intent rows.
/// @param hitClientId      ClientId of the hit target, or
///                         `k_missClientId` if the raycast missed.
/// @param hitX             Hit point X (or end of ray range on miss).
/// @param hitY             Hit point Y.
/// @param hitZ             Hit point Z.
/// @param hitRegion        Body-region enum value as int (head/torso/etc),
///                         0 if no hit.  Not strictly needed for hit-rate
///                         but useful for headshot-rate analysis later.
void recordShotResolution(std::uint16_t shooterClientId,
                          std::uint32_t shotInputTick,
                          std::uint16_t hitClientId,
                          float hitX,
                          float hitY,
                          float hitZ,
                          int hitRegion);

/// @brief Flush + close the log file.  Called from ServerGame::shutdown.
void close() noexcept;

} // namespace group2::perf::shotlog
