/// @file PostMatchResult.hpp
/// @brief Final scoreboard snapshot shown after a match ends.

#pragma once

#include <string>
#include <vector>

struct PostMatchScoreRow
{
    int clientId = -1;
    std::string name;
    int kills = 0;
    int deaths = 0;
    bool isLocal = false;
};

struct PostMatchResult
{
    bool won = false;
    int winnerId = -1;
    std::vector<PostMatchScoreRow> rows;
};
