/// @file GravityAbility.cpp
/// @brief Gravity flip ability implementation.

#include "GravityAbility.hpp"

#include "ecs/abilities/AbilityTuning.hpp"
#include "ecs/components/PlayerVisState.hpp"

AbilityType GravityAbility::type() const
{
    return AbilityType::Gravity;
}

float GravityAbility::cooldown() const
{
    return abilities::cooldownFor(type());
}

bool GravityAbility::canUse(entt::entity player, Registry& registry) const
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

void GravityAbility::activate(entt::entity player, Registry& registry)
{
    auto& vis = registry.get<PlayerVisState>(player);
    auto& abilState = registry.get<AbilityState>(player);

    vis.gravityFlipped = !vis.gravityFlipped;
    vis.grounded = false;

    setAbilityCooldown(abilState, type(), cooldown());
}
