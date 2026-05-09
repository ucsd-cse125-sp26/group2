/// @file AbilitySystem.hpp
/// @brief Ability Management System.

#pragma once
#include "ecs/abilities/AbilityRegistry.hpp"
#include "ecs/registry/Registry.hpp"

namespace systems
{

constexpr float dmgThreshold = 10000.0f;
constexpr int maxLevel = 2;

void runAbility(Registry& registry, AbilityRegistry& abilityRegistry, float dt);

}


