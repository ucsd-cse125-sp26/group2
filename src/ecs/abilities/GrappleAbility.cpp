/// @file GrappleAbility.cpp
/// @brief Grapple ability implementation.

#include "GrappleAbility.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerSimState.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/physics/SweptCollision.hpp"
#include "ecs/physics/TitanfallConstants.hpp"
#include "ecs/physics/WorldData.hpp"

#include <cmath>
#include <glm/geometric.hpp>

namespace
{

glm::vec3 lookDirFromInput(const InputSnapshot& input)
{
    const float cosPitch = std::cos(input.pitch);
    return {
        std::sin(input.yaw) * cosPitch,
        -std::sin(input.pitch),
        std::cos(input.yaw) * cosPitch,
    };
}

} // namespace

AbilityType GrappleAbility::type() const
{
    return AbilityType::Grapple;
}

float GrappleAbility::cooldown() const
{
    return tms::k_grappleCooldown;
}

bool GrappleAbility::canUse(entt::entity player, Registry& registry) const
{
    const auto* vis = registry.try_get<PlayerVisState>(player);
    const auto* sim = registry.try_get<PlayerSimState>(player);

    if (vis == nullptr || sim == nullptr) {
        return false;
    }

    if (vis->isDead) {
        return false;
    }

    if (vis->grappleActive) {
        return false;
    }

    if (sim->grappleCooldownActive) {
        return false;
    }

    return registry.all_of<Position, CollisionShape, InputSnapshot>(player);
}

void GrappleAbility::activate(entt::entity player, Registry& registry)
{
    auto& pos = registry.get<Position>(player);
    auto& shape = registry.get<CollisionShape>(player);
    auto& input = registry.get<InputSnapshot>(player);
    auto& vis = registry.get<PlayerVisState>(player);
    auto& sim = registry.get<PlayerSimState>(player);

    const glm::vec3 eye = pos.value + glm::vec3{0.0f, shape.halfExtents.y * 0.75f, 0.0f};
    const glm::vec3 forward = lookDirFromInput(input);
    const glm::vec3 end = eye + forward * tms::k_grappleMaxRange;

    const physics::SphereHitResult hit = physics::sphereCast(
        4.0f,
        eye,
        end,
        physics::activeWorld()
    );

    if (!hit.hit) {
        return;
    }

    vis.grappleActive = true;
    vis.grapplePoint = hit.point;
    vis.grounded = false;

    sim.grapplePullTimer = 0.0f;
    sim.grapplePullDir = glm::normalize(hit.point - eye);
}