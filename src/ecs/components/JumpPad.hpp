/// @file JumpPad.hpp
/// @brief Jump pad trigger information.

#pragma once

#include <glm/vec3.hpp>

/// @brief ECS component: world jump pad that imparts an instantaneous
/// velocity to any player whose AABB overlaps the pad's trigger volume.
///
/// The pad is authored in Blender as an empty/mesh with custom
/// properties `entity_type = 3` and optional `jump_velocity_y` (per-axis
/// overrides `jump_velocity_x`, `jump_velocity_z` are also recognised).
/// The map loader fills the position from the Blender transform and
/// `ServerGame` instantiates an entity with this component.
struct JumpPad
{
    /// @brief Velocity (units/s) applied to the player on entry.
    /// Y is the dominant component — a typical pad sets it to 1500
    /// and leaves XZ at zero so the player is launched straight up.
    glm::vec3 velocity{0.0f, 1500.0f, 0.0f};
};
