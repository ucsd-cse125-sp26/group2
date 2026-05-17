/// @file PowerupState.hpp
/// @brief Powerup state information.

#pragma once
#include "PowerupSpawner.hpp"

#include <array>
#include <cstddef>
#include <vector>

struct ActivePowerup
{
    PowerupType type;
    float timeRemaining = 0.0f;
};

struct PowerupState
{
    std::vector<ActivePowerup> active;
};

struct PowerupConfig
{
    PowerupType type = PowerupType::Shield;
    float duration = 10.0f;
    float spawnCooldown = 20.0f;
    float amount = 0.0f;
};

inline const PowerupConfig& getPowerupConfig(PowerupType type)
{
    static constexpr std::array<PowerupConfig, 7> k_kPowerupConfigs{{
        PowerupConfig{
            .type = PowerupType::Damage,
            .duration = 15.0f,
            .spawnCooldown = 240.0f,
            .amount = 2.0f,
        }, // Damage
        PowerupConfig{
            .type = PowerupType::Shield,
            .duration = 30.0f,
            .spawnCooldown = 240.0f,
            .amount = 200.0f, // amount of over shield
        },                    // Shield
    }};

    return k_kPowerupConfigs[static_cast<std::size_t>(type)];
}