/// @file AbilityState.hpp
/// @brief ECS component that tracks abilities and ability level

#pragma once

enum class AbilitySlot {
    Primary,
    Secondary
};

enum class AbilityType {
    None,
    Dash,
    Grapple,
    Gravity,
    Recall
};

struct AbilityState {
    int level = 0;
    float accumDamage = 0;

    AbilityType primary = AbilityType::None;
    AbilityType secondary = AbilityType::None;

    float primaryCooldown = 0.0f;
    bool primaryActive = false;

    float secondaryCooldown = 0.0f;
    bool secondaryActive = false;
};