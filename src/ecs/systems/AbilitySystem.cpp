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
        [&registry, &abilityRegistry, dt](entt::entity e, InputSnapshot& snap, const AbilityState& state) {
            if (snap.ability1) {
                useAbility(e, state.primary, registry, abilityRegistry);
            }

            if (snap.ability2) {
                useAbility(e, state.secondary, registry, abilityRegistry);
            }
        });
}

} // namespace systems
