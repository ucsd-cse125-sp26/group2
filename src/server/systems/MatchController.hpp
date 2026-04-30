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

    /// @brief Send the current match phase, countdown, and winner to all clients.
    /// @param server  Network server for broadcasting.
    void broadcastMatchState(Server& server);
};
