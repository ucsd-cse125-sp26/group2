/// @file AbilityRegistry.cpp
/// @brief Stores every ability available in the game and maps type to ability.

#include "AbilityRegistry.hpp"

void AbilityRegistry::registerAbility(std::unique_ptr<Ability> ability)
{
    if (!ability) {
        return;
    }

    const AbilityType type = ability->type();
    abilities[type] = std::move(ability);
}

Ability* AbilityRegistry::getAbility(const AbilityType type)
{
    const auto it = abilities.find(type);

    if (it == abilities.end()) {
        return nullptr;
    }

    return it->second.get();
}

bool AbilityRegistry::hasAbility(const AbilityType type) const
{
    return abilities.contains(type);
}
