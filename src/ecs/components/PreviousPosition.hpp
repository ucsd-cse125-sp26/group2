#pragma once

#include <glm/vec3.hpp>

/// @brief Copy of Position from the previous physics tick, used for render interpolation.
///
/// Written by the game loop immediately before each physics step:
/// @code
///   registry.get<PreviousPosition>(e).value = registry.get<Position>(e).value;
/// @endcode
///
/// Read by the renderer to interpolate between ticks:
/// @code
///   renderPos = glm::mix(prev.value, cur.value, alpha);
/// @endcode
///
/// @note Only the client needs this component. The server has no renderer.
struct PreviousPosition
{
    glm::vec3 value{0.0f, 0.0f, 0.0f}; ///< World-space position from the previous tick.
};
