/// @file DashAbility.hpp
/// @brief Defines the dash ability implementation.

#pragma once

#include "Ability.hpp"

class DashAbility : public Ability
{
public:
    AbilityType type() const override;
    float cooldown() const override;

    bool canUse(entt::entity player, Registry& registry) const override;
    void activate(entt::entity player, Registry& registry) override;
};
