/// @file Explosion.hpp
/// @brief Transient explosion request component.

#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

/// @brief Pending explosion to process this tick.
struct Explosion
{
    glm::vec3 position{0.0f};        ///< World-space center of the explosion.
    float radius = 0.0f;             ///< Blast radius (damage falls off via `falloffExponent`).
    float maxDamage = 0.0f;          ///< Maximum damage at the epicenter.
    float falloffExponent = 1.0f;    ///< Damage curve exponent: `damage = maxDamage * pow(1 - d/r, exp)`.
                                     ///< 1.0 = linear, 3.0 = cubic (sharp falloff).
    entt::entity owner = entt::null; ///< Entity that caused the explosion (for kill credit).
};
