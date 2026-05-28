/// @file HealthPackSpawner.hpp
/// @brief Health spawner information.

#pragma once

/// @brief ECS component: world weapon pickup point with respawn cooldown.
struct HealthPackSpawner
{
    float healAmount = 75.0f;
    float spawnCooldown = 0;             ///< Seconds remaining before the weapon reappears.
    bool hasPack = false;              ///< True if a weapon is available for pickup.
};
