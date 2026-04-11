#pragma once

#include <glm/glm.hpp>

/// @brief Component attached to fast-bullet projectile entities.
///
/// The particle system reads this each frame to update the oriented-capsule
/// streak (tip = current pos, tail = tip - direction * streakLength).
struct TracerEmitter
{
    glm::vec3 prevPos{};                         ///< Position from previous frame (tail anchor).
    float radius = 0.6f;                         ///< Cross-section half-width in world units.
    glm::vec4 coreColor{1.f, 0.95f, 0.7f, 1.f};  ///< Bright yellow-white hot core.
    glm::vec4 edgeColor{1.f, 0.40f, 0.05f, 0.f}; ///< Orange glow, alpha=0 at edge.
};
