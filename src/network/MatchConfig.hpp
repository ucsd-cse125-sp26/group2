#pragma once

struct MatchConfig
{
    int killsToWin = 25;                            ///< Number of kills required to win a match.
    int maxPlayers = 8;                             ///< Maximum number of connected players accepted by the server.
    float powerupInitialSpawnDelaySeconds = 240.0f; ///< Delay before powerups first appear after server start.
    float powerupRespawnCooldownSeconds = 30.0f;    ///< Delay before a picked-up powerup reappears.
};
