/// @file Explosion.hpp
/// @brief Transient explosion request component.

#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

/// @brief Pending explosion to process this tick.
struct Explosion
{
    glm::vec3 position{0.0f};        ///< World-space center of the explosion.
    float radius = 0.0f;             ///< Blast radius (damage falls off linearly).
    float maxDamage = 0.0f;          ///< Maximum damage at the epicenter.
    entt::entity owner = entt::null; ///< Entity that caused the explosion (for kill credit).
};
