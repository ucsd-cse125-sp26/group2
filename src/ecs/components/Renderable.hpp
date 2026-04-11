/// @file Renderable.hpp
/// @brief Renderable component linking ECS entities to renderer model instances.

#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

/// @brief Lightweight handle into the Renderer's models[] vector.
///
/// Attached to ECS entities that should be drawn in the world.
/// The Renderer looks up ModelInstance by this index each frame.
struct Renderable
{
    int32_t modelIndex = -1;           ///< Index into Renderer::models[]. -1 = no model.
    glm::vec3 scale{1.0f};             ///< Per-entity scale override.
    glm::quat orientation{1, 0, 0, 0}; ///< Per-entity rotation override (identity by default).
    bool visible = true;               ///< False to skip rendering without removing the component.
};
