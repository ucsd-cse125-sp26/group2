/// @file Velocity.hpp
/// @brief Linear velocity component for ECS entities.

#pragma once

#include <glm/vec3.hpp>

/// @brief Linear velocity of an entity, in game units per second.
struct Velocity
{
    glm::vec3 value{0.0f, 0.0f, 0.0f}; ///< XYZ velocity (Y-up, units/s).
};
