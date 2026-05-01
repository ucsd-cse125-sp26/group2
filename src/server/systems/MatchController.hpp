/// @file MatchController.hpp
/// @brief Controls match flow, including starting and ending matches and managing match state.
#pragma once

#include "ecs/registry/Registry.hpp"
#include "network/MatchStatus.hpp"
#include "network/Server.hpp"

/// @brief Manages match flow: warmup → countdown → in-progress → finished → warmup.
///
/// Transitions between phases based on player count and kill thresholds.
/// Broadcasts match state to all clients every tick.
class MatchController
{
public:
    /// @brief Advance the match state machine by one tick.
    /// @param deltaTime  Fixed physics delta time in seconds.
    /// @param registry   The ECS registry (for win condition checks).
    /// @param server     Network server (for broadcasting state).
    void update(float deltaTime, Registry& registry, Server& server);

    /// @brief Return the current match phase.
    MatchPhase getCurrentPhase();

    /// @brief Return the winner's client ID, or -1 if no winner yet.
    int getWinnerId();

private:
    MatchPhase currentPhase = MatchPhase::WARMUP;      ///< Current phase of the match.
    float countdownTimer = 0.0f;                       ///< Seconds remaining in the current timed phase.
    int winnerId = -1;                                 ///< Client ID of the winner (-1 if none).

    static constexpr float k_countdownDuration = 5.0f; ///< Seconds for the pre-match countdown.
    static constexpr float k_finishedDuration = 5.0f;  ///< Seconds to display results before reset.
    static constexpr int k_killsToWin = 10;            ///< Kill threshold to win the match.

    // PR-4 (server-perf): match-state replicate-on-change.
    //
    // Pre-PR-4 the controller broadcast `MATCH_STATE` every tick
    // (128 Hz) regardless of whether anything changed — at 200 bots
    // that was 25.6 k enqueues/sec all under stateMutex_, which
    // dominated the `match` scope's 100+ ms p99 at 200 bots in PR-3.
    // The on-wire payload is tiny (a few bytes) and the only thing
    // that ever changes meaningfully is `currentPhase` plus the
    // countdownTimer (which the client smoothes locally between
    // ticks). We now broadcast only when those values move enough
    // to be observable on the client.
    MatchPhase lastBroadcastPhase = MatchPhase::WARMUP;
    int lastBroadcastWinnerId = -1;
    float lastBroadcastCountdown = -1.0f;
    int ticksSinceBroadcast = 0;

    /// @brief Send the current match phase, countdown, and winner to
    /// all clients. PR-4: skip the actual broadcast when nothing
    /// observable has changed since the last send.
    /// @param server  Network server for broadcasting.
    void broadcastMatchState(Server& server);
};
