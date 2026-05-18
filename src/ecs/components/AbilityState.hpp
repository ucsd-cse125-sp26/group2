/// @file AbilityState.hpp
/// @brief ECS component that tracks abilities and ability level

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/vec3.hpp>

enum class AbilitySlot : uint8_t
{
    Primary,
    Secondary
};

enum class AbilityType : uint8_t
{
    None,
    Dash,
    Grapple,
    Gravity,
    Recall
};

inline constexpr std::size_t kAbilityChoicesPerTier = 2;

inline constexpr std::array<AbilityType, kAbilityChoicesPerTier> primaryAbilityTypes = {
    AbilityType::Dash,
    AbilityType::Grapple,
};

inline constexpr std::array<AbilityType, kAbilityChoicesPerTier> secondaryAbilityTypes = {
    AbilityType::Gravity,
    AbilityType::Recall,
};

inline constexpr const char* abilityName(AbilityType type)
{
    switch (type) {
    case AbilityType::Dash:
        return "DASH";
    case AbilityType::Grapple:
        return "GRAPPLE";
    case AbilityType::Gravity:
        return "GRAVITY";
    case AbilityType::Recall:
        return "RECALL";
    case AbilityType::None:
    default:
        return "LOCKED";
    }
}

inline constexpr const char* abilityDescription(AbilityType type)
{
    switch (type) {
    case AbilityType::Dash:
        return "Burst in your movement direction.";
    case AbilityType::Grapple:
        return "Hook terrain and pull yourself in.";
    case AbilityType::Gravity:
        return "Flip gravity and fight from the ceiling.";
    case AbilityType::Recall:
        return "Mark a spot, then return to it.";
    case AbilityType::None:
    default:
        return "No ability selected.";
    }
}

struct AbilityState
{
    int level = 0;
    float accumDamage = 0;
    bool pendingLevel1 = false;
    bool pendingLevel2 = false;

    std::array<AbilityType, kAbilityChoicesPerTier> primaryChoices = primaryAbilityTypes;
    std::array<AbilityType, kAbilityChoicesPerTier> secondaryChoices = secondaryAbilityTypes;

    AbilityType primary = AbilityType::None;
    AbilityType secondary = AbilityType::None;

    float primaryCooldown = 0.0f;
    bool primaryActive = false;

    float secondaryCooldown = 0.0f;
    bool secondaryActive = false;

    bool recallMarkerSet = false;
    bool recallMarkerGravityFlipped = false;
    glm::vec3 recallMarkerPosition{0.0f};

    AbilitySlot nextReselectSlot = AbilitySlot::Primary;
};

inline bool hasPendingAbilitySelection(const AbilityState& state)
{
    return state.pendingLevel1 || state.pendingLevel2;
}

inline AbilitySlot pendingAbilitySlot(const AbilityState& state)
{
    return state.pendingLevel1 ? AbilitySlot::Primary : AbilitySlot::Secondary;
}

inline const std::array<AbilityType, kAbilityChoicesPerTier>& choicesForPendingSelection(const AbilityState& state)
{
    return state.pendingLevel1 ? state.primaryChoices : state.secondaryChoices;
}

inline void choosePendingAbility(AbilityState& state, std::size_t choiceIndex)
{
    choiceIndex = std::min(choiceIndex, kAbilityChoicesPerTier - 1);

    if (state.pendingLevel1) {
        state.primary = state.primaryChoices[choiceIndex];
        state.primaryCooldown = 0.0f;
        state.primaryActive = false;
        state.pendingLevel1 = false;
        return;
    }

    if (state.pendingLevel2) {
        state.secondary = state.secondaryChoices[choiceIndex];
        state.secondaryCooldown = 0.0f;
        state.secondaryActive = false;
        state.recallMarkerSet = false;
        state.pendingLevel2 = false;
    }
}

inline bool isAbilityOnCooldown(const AbilityState& state, AbilityType type)
{
    if (state.primary == type && state.primaryCooldown > 0.0f) {
        return true;
    }

    if (state.secondary == type && state.secondaryCooldown > 0.0f) {
        return true;
    }

    return false;
}

inline void setAbilityCooldown(AbilityState& state, AbilityType type, float cooldown)
{
    if (state.primary == type) {
        state.primaryCooldown = cooldown;
    }

    if (state.secondary == type) {
        state.secondaryCooldown = cooldown;
    }
}

inline float abilityCooldownRemaining(const AbilityState& state, AbilityType type)
{
    if (state.primary == type) {
        return state.primaryCooldown;
    }

    if (state.secondary == type) {
        return state.secondaryCooldown;
    }

    return 0.0f;
}
