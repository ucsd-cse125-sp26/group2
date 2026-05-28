#pragma once

struct MatchConfig
{
    int killsToWin = 10; ///< Number of kills required to win a match.
    int maxPlayers = 8;  ///< Maximum number of connected players accepted by the server.
};
