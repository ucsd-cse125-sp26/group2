#pragma once

#include <glm/vec3.hpp>

// Linear velocity of an entity, in game units per second.
struct Velocity
{
    glm::vec3 value{0.0f, 0.0f, 0.0f};
};
