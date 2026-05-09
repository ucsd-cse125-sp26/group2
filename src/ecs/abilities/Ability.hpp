/// @file Ability.hpp
/// @brief Defines the shared interface every ability implements.

#pragma once
#include "ecs/components/AbilityState.hpp"
#include "ecs/registry/Registry.hpp"

class Ability
{
    public:
        virtual ~Ability() = default;

        virtual AbilityType type() const = 0;
        virtual float cooldown() const = 0;
        virtual bool canUse(entt::entity player, Registry& registry) const = 0;
        virtual void activate(entt::entity player, Registry& registry) = 0;
};