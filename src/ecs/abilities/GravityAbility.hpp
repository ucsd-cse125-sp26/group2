/// @file GravityAbility.hpp
/// @brief Defines the gravity flip ability implementation.

#pragma once

#include "Ability.hpp"

class GravityAbility : public Ability
{
public:
    AbilityType type() const override;
    float cooldown() const override;

    bool canUse(entt::entity player, Registry& registry) const override;
    void activate(entt::entity player, Registry& registry) override;
};
