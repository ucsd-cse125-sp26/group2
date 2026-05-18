/// @file AbilitySystem.hpp
/// @brief Ability Management System.

#pragma once
#include "ecs/abilities/AbilityRegistry.hpp"
#include "ecs/components/AbilityState.hpp"
#include "ecs/registry/Registry.hpp"

namespace systems
{

constexpr float dmgThreshold = 1000.0f;
constexpr int maxLevel = 2;

void grantAbilityLevel(AbilityState& state);
void grantAbilityProgress(AbilityState& state, float amount);
void runAbility(Registry& registry, AbilityRegistry& abilityRegistry, float dt);

} // namespace systems
