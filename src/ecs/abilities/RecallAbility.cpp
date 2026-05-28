/// @file RecallAbility.cpp
/// @brief Recall marker/teleport ability implementation.

#include "RecallAbility.hpp"

#include "ecs/abilities/AbilityTuning.hpp"
#include "ecs/components/PlayerSimState.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"

AbilityType RecallAbility::type() const
{
    return AbilityType::Recall;
}

float RecallAbility::cooldown() const
{
    return abilities::cooldownFor(type());
}

bool RecallAbility::canUse(entt::entity player, Registry& registry) const
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

    return registry.all_of<Position, Velocity>(player);
}

void RecallAbility::activate(entt::entity player, Registry& registry)
{
    auto& pos = registry.get<Position>(player);
    auto& vel = registry.get<Velocity>(player);
    auto& vis = registry.get<PlayerVisState>(player);
    auto& abilState = registry.get<AbilityState>(player);

    if (!abilState.recallMarkerSet) {
        abilState.recallMarkerPosition = pos.value;
        abilState.recallMarkerGravityFlipped = vis.gravityFlipped;
        abilState.recallMarkerSet = true;
        return;
    }

    pos.value = abilState.recallMarkerPosition;
    vel.value = glm::vec3{0.0f};
    vis.gravityFlipped = abilState.recallMarkerGravityFlipped;
    vis.grounded = false;
    vis.grappleActive = false;
    vis.moveMode = MoveMode::OnFoot;
    vis.exitingWall = false;

    if (auto* sim = registry.try_get<PlayerSimState>(player)) {
        sim->lastSafePosition = pos.value;
        sim->lastSafePositionValid = true;
    }

    abilState.recallMarkerSet = false;
    setAbilityCooldown(abilState, type(), cooldown());
}
