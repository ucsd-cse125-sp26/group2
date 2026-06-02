/// @file WallhackAbility.hpp
/// @brief Tier-2 ability: briefly reveal enemies through walls (red chams).

#pragma once

#include "Ability.hpp"

/// @brief Activates a timed "see enemies through walls" reveal. The effect is a
/// purely visual client-side chams pass; this ability just owns the
/// authoritative timer + cooldown (replicated via AbilityState).
class WallhackAbility : public Ability
{
public:
    AbilityType type() const override;
    float cooldown() const override;

    bool canUse(entt::entity player, Registry& registry) const override;
    void activate(entt::entity player, Registry& registry) override;
};
