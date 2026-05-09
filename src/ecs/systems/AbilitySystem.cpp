/// @file AbilitySystem.hpp
/// @brief Ability Management System.

#include "AbilitySystem.hpp"

#include "ecs/components/AbilityState.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Player.hpp"
#include "ecs/registry/Registry.hpp"

namespace systems
{

void runAbility(Registry& registry, float dt)
{
    registry.view<Player, InputSnapshot, AbilityState>().each([&registry, dt](entt::entity e, InputSnapshot& snap, const AbilityState& state) {
        // Run ability 1
        // Run ability 2
    });
}

}
