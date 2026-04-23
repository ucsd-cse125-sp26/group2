/// @file MatchStatus.hpp
/// @brief Shared definitions for match status and state synchronization between server and clients.
#pragma once

enum class MatchPhase : uint8_t
{
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
