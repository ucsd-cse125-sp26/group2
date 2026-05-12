/// @file AbilityState.hpp
/// @brief ECS component that tracks abilities and ability level

#pragma once

#include <vector>

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

const std::vector<AbilityType> primaryAbilityTypes = {
    AbilityType::Dash,
    AbilityType::Grapple,
};

const std::vector<AbilityType> secondaryAbilityTypes = {
    AbilityType::Gravity,
    AbilityType::Recall,
};

struct AbilityState {
    int level = 0;
    float accumDamage = 0;
    bool pendingLevel1 = false;
    bool pendingLevel2 = false;

    AbilityType primary = AbilityType::None;
    AbilityType secondary = AbilityType::None;

    float primaryCooldown = 0.0f;
    bool primaryActive = false;

    float secondaryCooldown = 0.0f;
    bool secondaryActive = false;
};