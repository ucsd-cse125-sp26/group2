/// @file GravityAbility.cpp
/// @brief Gravity flip ability implementation.

#include "GravityAbility.hpp"

#include "ecs/components/PlayerVisState.hpp"
#include "ecs/physics/PhysicsConstants.hpp"

AbilityType GravityAbility::type() const
{
    return AbilityType::Gravity;
}

float GravityAbility::cooldown() const
{
    return physics::k_gravityFlipCooldown;
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

    if (abilState->primary == type() && abilState->primaryCooldown > 0.0f) {
        return false;
    }

    if (abilState->secondary == type() && abilState->secondaryCooldown > 0.0f) {
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

    if (abilState.primary == type()) {
        abilState.primaryCooldown = cooldown();
    }

    if (abilState.secondary == type()) {
        abilState.secondaryCooldown = cooldown();
    }
}
