/// @file RespawnTimer.hpp
/// @brief Component for tracking player respawn times.

#pragma once

/// @brief ECS component: countdown until a dead player respawns.
///
/// Attached on death, removed by handleRespawn() when the timer expires.
struct RespawnTimer
{
    float timeRemaining; ///< Seconds remaining until respawn.
};
