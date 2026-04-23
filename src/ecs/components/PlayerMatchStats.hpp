/// @file PlayerMatchStats.hpp
/// @brief Component for player match-related stats

#pragma once
struct PlayerMatchStats
{
    int score = 0;       // Player's current score
    int kills = 0;       // Number of kills player has achieved
    int deaths = 0;      // Number of times player has died
    bool hasWon = false; // Whether the player has won the match (e.g. reached kill threshold)
};
