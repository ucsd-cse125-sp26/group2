/// @file WallhackAbility.cpp
/// @brief Tier-2 wallhack reveal implementation.

#include "WallhackAbility.hpp"

#include "ecs/abilities/AbilityTuning.hpp"
#include "ecs/components/PlayerVisState.hpp"

AbilityType WallhackAbility::type() const
{
    return AbilityType::Wallhack;
}

float WallhackAbility::cooldown() const
{
    return abilities::cooldownFor(type());
}

bool WallhackAbility::canUse(entt::entity player, Registry& registry) const
{
    const auto* vis = registry.try_get<PlayerVisState>(player);
    const auto* abilState = registry.try_get<AbilityState>(player);

    if (vis == nullptr || abilState == nullptr) {
        return false;
    }
    if (vis->isDead) {
        return false;
    }
    if (isAbilityOnCooldown(*abilState, type())) {
        return false;
    }
    return true;
}

void WallhackAbility::activate(entt::entity player, Registry& registry)
{
    auto& abilState = registry.get<AbilityState>(player);

    // Start the reveal window; the ability system ticks it down. Replicated via
    // AbilityState so the local client can flag enemies for the chams pass.
    abilState.wallhackTimer = abilities::k_wallhackDuration;

    setAbilityCooldown(abilState, type(), cooldown());
}
