/// @file DashAbility.cpp
/// @brief Dash ability implementation.

#include "DashAbility.hpp"

#include "ecs/abilities/AbilityTuning.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Movement.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace
{

glm::vec3 dashDirFromInput(const InputSnapshot& input)
{
    glm::vec3 dir = physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
    if (glm::length(dir) > 0.001f) {
        return glm::normalize(dir);
    }

    return glm::normalize(glm::vec3{std::sin(input.yaw), 0.0f, std::cos(input.yaw)});
}

} // namespace

AbilityType DashAbility::type() const
{
    return AbilityType::Dash;
}

float DashAbility::cooldown() const
{
    return abilities::cooldownFor(type());
}

bool DashAbility::canUse(entt::entity player, Registry& registry) const
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

    if (vis->grappleActive) {
        return false;
    }

    return registry.all_of<Velocity, CollisionShape, InputSnapshot>(player);
}

void DashAbility::activate(entt::entity player, Registry& registry)
{
    auto& input = registry.get<InputSnapshot>(player);
    auto& vis = registry.get<PlayerVisState>(player);
    auto& vel = registry.get<Velocity>(player);
    auto& abilState = registry.get<AbilityState>(player);

    const glm::vec3 dir = dashDirFromInput(input);
    const glm::vec3 localUp = vis.gravityFlipped ? glm::vec3{0.0f, -1.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const float upwardSpeed = std::max(0.0f, glm::dot(vel.value, localUp));

    vel.value = dir * abilities::k_dashSpeed + localUp * std::max(upwardSpeed, abilities::k_dashLift);

    vis.grounded = false;
    vis.grappleActive = false;
    vis.moveMode = MoveMode::OnFoot;
    vis.exitingWall = false;
    vis.exitingClimb = false;

    setAbilityCooldown(abilState, type(), cooldown());
}
