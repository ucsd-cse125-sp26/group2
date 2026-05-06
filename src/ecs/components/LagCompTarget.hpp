/// @file LagCompTarget.hpp
/// @brief Per-shooter rewind target for lag-compensated hitscan.
///
/// Set on every server-side player entity each tick by the lag-comp
/// scheduler from the client's reported RTT. Read by `rewindHitboxes`
/// at the top of every hitscan call site to decide whether — and how
/// far — to rewind other entities' capsules. Server-only in practice
/// (no system populates it on the client; the type lives in shared
/// code so the rewind helper compiles into both binaries).
///
/// `targetServerTick == 0` is the explicit "no rewind" sentinel — used
/// for brand-new connections that haven't completed their first ping
/// round-trip and for clients with sub-tick RTT (loopback, LAN). The
/// rewind helper short-circuits to a no-op guard in both cases.

#pragma once

#include <cstdint>

/// @brief Server tick to which to rewind other entities' hitboxes when
/// resolving this entity's hitscan. Zero = no rewind.
struct LagCompTarget
{
    uint32_t targetServerTick = 0;

    /// @brief PR-22: lag-compensation distance in ticks
    /// (`currentServerTick − targetServerTick`).  Stored alongside the
    /// target so the shot-log path can record it without plumbing the
    /// per-tick `tickCount` into `WeaponSystem::handleFire`.  Zero
    /// when no rewind requested.  Diagnostic only — `rewindHitboxes`
    /// reads `targetServerTick` directly.
    uint16_t lagTicks = 0;

    /// @brief PR-22: shooter's most recent reported RTT in ms.  Used
    /// by the netsync hit-reg analyzer to bucket shots by RTT without
    /// having to join against the per-client net-stats stream.  Zero
    /// before the first ping round-trip completes.
    uint16_t rttMs = 0;
};
