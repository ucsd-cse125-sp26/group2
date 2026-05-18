/// @file AbilitySystem.hpp
/// @brief Ability Management System.

#include "AbilitySystem.hpp"

#include "ecs/abilities/Ability.hpp"
#include "ecs/abilities/AbilityRegistry.hpp"
#include "ecs/components/AbilityState.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/registry/Registry.hpp"

namespace systems
{

inline void tickCooldown(float& cooldown, float dt)
{
    if (cooldown <= 0.0f) {
        return;
    }

    cooldown -= dt;
    if (cooldown < 0.0f) {
        cooldown = 0.0f;
    }
}

inline void useAbility(entt::entity player, AbilityType type, Registry& registry, AbilityRegistry& abilityRegistry)
{
    if (type == AbilityType::None) {
        return;
    }

    Ability* ability = abilityRegistry.getAbility(type);
    if (ability == nullptr) {
        return;
    }

    if (!ability->canUse(player, registry)) {
        return;
    }

    ability->activate(player, registry);
}

void runAbility(Registry& registry, AbilityRegistry& abilityRegistry, float dt)
{
    registry.view<Player, InputSnapshot, AbilityState>().each(
        [&registry, &abilityRegistry, dt](entt::entity e, InputSnapshot& snap, AbilityState& state) {
            tickCooldown(state.primaryCooldown, dt);
            tickCooldown(state.secondaryCooldown, dt);

            if (snap.ability1 && !state.primaryActive) {
                useAbility(e, state.primary, registry, abilityRegistry);
            }

            if (snap.ability2 && !state.secondaryActive) {
                useAbility(e, state.secondary, registry, abilityRegistry);
            }

            state.primaryActive = snap.ability1;
            state.secondaryActive = snap.ability2;
        });
}

} // namespace systems
