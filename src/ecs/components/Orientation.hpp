/// @file Orientation.hpp
/// @brief Orientation + angular velocity components for dynamic rigid bodies.
///
/// Kept separate from `Position` to avoid disturbing the existing
/// `Position { glm::vec3 value }` layout (which is replicated heavily
/// across the codebase).  Entities without `Orientation` are treated as
/// fixed-orientation kinematic / static bodies — most of the game today.

#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

/// @brief World-space orientation as a unit quaternion.  Identity = no rotation.
struct Orientation
{
    glm::quat value{1.0f, 0.0f, 0.0f, 0.0f};
};

/// @brief Body-space angular velocity in radians per second (axis-magnitude).
struct AngularVelocity
{
    glm::vec3 value{0.0f};
};
