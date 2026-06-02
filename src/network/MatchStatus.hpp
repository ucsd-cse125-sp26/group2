/// @file MatchStatus.hpp
/// @brief Shared definitions for match status and state synchronization between server and clients.
#pragma once

#include <cstdint>

enum class MatchPhase : uint8_t
{
    LOBBY, ///< Pre-match lobby phase; players are not yet spawned in-world.
    WARMUP,
    COUNTDOWN,
    IN_PROGRESS,
    FINISHED
};

struct MatchStatePacket
{
    MatchPhase phase;
    float countdownTimer; // Time remaining in current phase, if applicable
    int winnerId;         // Player ID of winner, if in FINISHED phase
};
