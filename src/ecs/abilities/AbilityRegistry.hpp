/// @file AbilityRegistry.hpp
/// @brief Stores every ability available in the game and maps type to ability.

#pragma once

#include "Ability.hpp"

#include <memory>
#include <unordered_map>

class AbilityRegistry
{
public:
    AbilityRegistry() = default;

    void registerAbility(std::unique_ptr<Ability> ability);

    Ability* getAbility(AbilityType type);

    bool hasAbility(AbilityType type) const;

private:
    std::unordered_map<AbilityType, std::unique_ptr<Ability>> abilities;
};
