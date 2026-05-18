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

inline bool queueAbilityChoice(AbilityState& state)
{
    if (hasPendingAbilitySelection(state)) {
        return false;
    }

    if (state.level < maxLevel) {
        state.level += 1;
        if (state.level == 1) {
            state.pendingLevel1 = true;
        } else {
            state.pendingLevel2 = true;
            state.nextReselectSlot = AbilitySlot::Primary;
        }
        return true;
    }

    if (state.nextReselectSlot == AbilitySlot::Primary) {
        state.pendingLevel1 = true;
        state.nextReselectSlot = AbilitySlot::Secondary;
    } else {
        state.pendingLevel2 = true;
        state.nextReselectSlot = AbilitySlot::Primary;
    }

    return true;
}

void grantAbilityLevel(AbilityState& state)
{
    if (queueAbilityChoice(state)) {
        state.accumDamage = 0.0f;
    }
}

void grantAbilityProgress(AbilityState& state, float amount)
{
    if (amount <= 0.0f) {
        return;
    }

    state.accumDamage += amount;
    while (state.accumDamage >= dmgThreshold) {
        if (!queueAbilityChoice(state)) {
            state.accumDamage = dmgThreshold;
            return;
        }
        state.accumDamage -= dmgThreshold;
    }
}

inline void handleAbilitySelection(InputSnapshot& snap, AbilityState& state)
{
    if (!hasPendingAbilitySelection(state)) {
        snap.abilitySelectLeft = false;
        snap.abilitySelectRight = false;
        return;
    }

    if (snap.abilitySelectLeft || snap.abilitySelectRight) {
        choosePendingAbility(state, snap.abilitySelectRight ? 1 : 0);
        snap.abilitySelectLeft = false;
        snap.abilitySelectRight = false;
    }
}

void runAbility(Registry& registry, AbilityRegistry& abilityRegistry, float dt)
{
    registry.view<Player, InputSnapshot, AbilityState>().each(
        [&registry, &abilityRegistry, dt](entt::entity e, InputSnapshot& snap, AbilityState& state) {
            tickCooldown(state.primaryCooldown, dt);
            tickCooldown(state.secondaryCooldown, dt);

            if (snap.debugGrantAbilityLevel) {
                grantAbilityLevel(state);
                snap.debugGrantAbilityLevel = false;
            }

            handleAbilitySelection(snap, state);

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
