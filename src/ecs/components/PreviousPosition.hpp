#pragma once

#include <glm/vec3.hpp>

// Copy of Position from the previous physics tick.
//
// Written by the game loop immediately before each physics step:
//   registry.get<PreviousPosition>(e).value = registry.get<Position>(e).value;
//
// Read by the renderer to interpolate between ticks:
//   renderPos = glm::mix(prev.value, cur.value, alpha);
//
// Only the client needs this component. The server has no renderer.
struct PreviousPosition
{
    glm::vec3 value{0.0f, 0.0f, 0.0f};
};
