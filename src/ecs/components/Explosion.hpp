/// @file Explosion.hpp
/// @brief Transient explosion request component.

#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

/// @brief Pending explosion to process this tick.
struct Explosion
{
    glm::vec3 position{0.0f};
    float radius = 0.0f;
    float maxDamage = 0.0f;
    entt::entity owner = entt::null;
};
