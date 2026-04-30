/// @file PlayerMatchStats.hpp
/// @brief Component for player match-related stats

#pragma once
/// @brief ECS component: per-player scoreboard statistics for the current match.
struct PlayerMatchStats
{
    int score = 0;       ///< Player's current score.
    int kills = 0;       ///< Number of kills achieved this match.
    int deaths = 0;      ///< Number of deaths this match.
    bool hasWon = false; ///< True if the player reached the kill threshold.
};
