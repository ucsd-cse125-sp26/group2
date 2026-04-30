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
/// Capacity 32 covers ~250 ms at 128 Hz, which dominates the worst-case
/// one-way lag the server is willing to compensate for (cap is 200 ms in
/// the plan). At ~12 capsules × 32 samples × ~24 B/capsule per entity
/// ≈ 9 KB; with ~30 players the whole ring fits in ~270 KB — trivial
/// against the server's ECS heap.
struct HitboxHistory
{
    /// @brief Number of past samples retained per entity.
    ///
    /// Sized so that the highest-allowed compensated lag (200 ms) fits
    /// inside the ring at the physics tick rate (128 Hz → 25.6 ticks).
    /// Rounded up to the nearest power of two for cheap modulo.
    static constexpr std::size_t k_capacity = 32;

    std::array<HitboxHistorySample, k_capacity> ring{};

    /// @brief Index where the next push lands. Wraps modulo `k_capacity`.
    std::size_t head = 0;

    /// @brief Number of samples written so far, capped at `k_capacity`.
    /// Used so consumers know which slots are populated before the ring
    /// has wrapped around the first time.
    std::size_t count = 0;
};
