/// @file RespawnPoint.hpp
/// @brief Player spawn point component with cooldown state.

#pragma once

/// @brief ECS component: player respawn point with cooldown state.
///
/// After a player spawns at this point, it enters a cooldown period during
/// which other players prefer alternative spawn locations.  If all spawn
/// points are on cooldown, the one with the lowest remaining cooldown is
/// chosen.
struct RespawnPoint
{
    float cooldown = 0.0f; ///< Seconds remaining before the point is preferred again.
    bool available = true; ///< True when cooldown has elapsed (ready for use).
    float yaw = 0.0f;      ///< Authored facing direction (radians) applied to players spawning here.
};
