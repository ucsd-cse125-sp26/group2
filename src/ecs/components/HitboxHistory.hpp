/// @file HitboxHistory.hpp
/// @brief Server-side ring buffer of recent hitbox capsule snapshots per entity.
///
/// Phase 6 of the networking overhaul lays the plumbing for lag-compensated
/// hitscan: the server records the world-space hitbox capsules of each
/// player every physics tick into a per-entity ring, so when an attacker's
/// shot arrives we can (eventually) rewind targets to where they were on
/// the attacker's screen at the moment they pulled the trigger.
///
/// Today this component is **populated** by `HitboxHistorySystem` on the
/// server but **not consumed** anywhere — `rewindHitboxes` is a no-op
/// stub. The future "flip" is a one-line change inside the rewind guard
/// that swaps `HitboxInstance::capsules` for the stored sample matching
/// the attacker's tick. Wire format and gameplay are unchanged in Phase
/// 6; this is purely scaffolding that ships compiled, runs, and earns
/// its CPU budget so the eventual flip is risk-free.
///
/// The component is also defined in shared (ecs/) code so the rewind
/// guard's `view<HitboxHistory>` query compiles into both server and
/// client TUs. On the client, no entity ever has the component — the
/// query is empty and the guard is automatically a no-op.

#pragma once

#include "ecs/components/Hitbox.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/// @brief Per-tick capsule snapshot stored in the history ring.
///
/// The capsules vector is a copy of the entity's `HitboxInstance::capsules`
/// taken just after `updateHitboxes()` for the given tick. Storing the
/// world-space form (rather than the bone-local form + transform) keeps
/// the rewind code from re-running the bone math; it just swaps in the
/// vector and we ray-test it directly.
struct HitboxHistorySample
{
    uint32_t tick = 0; ///< Server tick when the sample was recorded. 0 = unset.
    std::vector<WorldCapsule> capsules;
};

/// @brief Ring buffer of recent hitbox snapshots for one entity.
///
/// PR-12 bumped capacity from 32 → 64 ticks (~500 ms @ 128 Hz) so the
/// ring covers the new worst-case rewind: RTT/2 (capped 200 ms = 25 ticks)
/// + client cl_interp (capped 8 snapshots × 4 ticks/snapshot = 32 ticks
/// @ 32 Hz snapshot rate) = 57 ticks.  Rounded up to the next power of
/// two (64) for cheap modulo arithmetic.  Still trivial space cost:
/// at ~12 capsules × 64 samples × ~24 B/capsule per entity ≈ 18 KB,
/// ~540 KB total at 30 players.
struct HitboxHistory
{
    /// @brief Number of past samples retained per entity.
    ///
    /// PR-12: sized so that the highest-allowed compensated lag
    /// (k_maxLagCompTicks = 64 ticks ≈ 500 ms @ 128 Hz) fits inside
    /// the ring.  Power of two for cheap modulo.
    static constexpr std::size_t k_capacity = 64;

    std::array<HitboxHistorySample, k_capacity> ring{};

    /// @brief Index where the next push lands. Wraps modulo `k_capacity`.
    std::size_t head = 0;

    /// @brief Number of samples written so far, capped at `k_capacity`.
    /// Used so consumers know which slots are populated before the ring
    /// has wrapped around the first time.
    std::size_t count = 0;
};
