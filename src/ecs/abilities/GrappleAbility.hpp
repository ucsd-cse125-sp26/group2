/// @file GrappleAbility.hpp
/// @brief Defines the grapple ability implementation.

#pragma once
#include "Ability.hpp"
#include "ecs/registry/Registry.hpp"

class GrappleAbility : public Ability
{
    public:
        AbilityType type() const override {
        return AbilityType::Grapple;
        }

        float cooldown() const override
        {
            return 5.0f; // in seconds
        }

        void activate(entt::entity player, Registry& registry) override
        {
            // dec cooldown and return if > 0
            // apply grapple movement
            // refresh cooldown
        }
};

