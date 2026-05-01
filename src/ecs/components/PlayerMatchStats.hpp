/// @file PlayerMatchStats.hpp
/// @brief Component for player match-related stats

#pragma once

#include <cstdint>

/// @brief ECS component: per-player scoreboard statistics for the current match.
///
/// Replicated to every client (it's in `RegistrySerialization::Synced`),
/// which is what makes per-player ping work in the scoreboard HUD: the
/// server stamps `rttMs` from each client's `Connection::lastReportedRttMs`
/// each tick, and every receiving client reads the *target's* `rttMs`
/// from this component instead of its own local `NetworkStats::rttMs`.
struct PlayerMatchStats
{
    int score = 0;       ///< Player's current score.
    int kills = 0;       ///< Number of kills achieved this match.
    int deaths = 0;      ///< Number of deaths this match.
    bool hasWon = false; ///< True if the player reached the kill threshold.

    /// @brief Last RTT this player's client reported to the server (ms).
    ///
    /// Source: the 2-byte `rttMs` prefix on every INPUT packet, which
    /// carries the client's smoothed `NetworkStats::avgRttMs`. Server
    /// copies it out of `Connection::lastReportedRttMs` into this
    /// component once per tick (in `updateLagCompTargets`), so it
    /// rides the existing snapshot stream to every client. 0 until
    /// the player's first INPUT arrives.
    uint16_t rttMs = 0;
};
