/// @file PowerupSpawner.hpp
/// @brief Powerup spawner information.

#pragma once
#include "glm/fwd.hpp"

enum class PowerupType : uint8_t
{
    Damage,
    Shield,
};

/// @brief ECS component: world powerup pickup point with respawn cooldown.
struct PowerupSpawner
{
    PowerupType type = PowerupType::Damage; ///< Type of powerup this spawner provides.
    float spawnCooldown = 0;                ///< Seconds remaining before the powerup reappears.
    bool hasPowerup = false;                ///< True if powerup is available for pickup.
    bool hasSpawnedOnce = false;            ///< True after the initial delayed spawn has completed.
};
