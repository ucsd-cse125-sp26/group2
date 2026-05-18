/// @file Health.hpp
/// @brief Player health and armor.

#pragma once

/// @brief ECS component: player health, armor, and passive heal cooldown.
struct Health
{
    float health = 100.0f;  ///< Current health points (0 = dead).
    float armor = 100.0f;   ///< Current armor points (absorbs damage before health).
    float overShield = 0.0f;
    float healTimer = 0.0f; ///< Seconds remaining before passive healing begins.
};
