/// @file Position.hpp
/// @brief World-space position component for ECS entities.

#pragma once

#include <glm/vec3.hpp>

/// @brief World-space position of an entity, in game units.
struct Position
{
    glm::vec3 value{0.0f, 0.0f, 0.0f}; ///< XYZ position (Y-up, Quake units).
};
