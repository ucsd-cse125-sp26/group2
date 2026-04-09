#pragma once

/// @brief Locomotion state flags for a player entity.
///
/// Read by MovementSystem to select the correct physics constants.
/// Written by CollisionSystem (`grounded`) and MovementSystem (`crouching`, `sliding`).
struct PlayerState
{
    bool grounded{false};  ///< True when touching a floor surface this tick.
    bool crouching{false}; ///< True when crouch input is held; CollisionShape.halfExtents.y is reduced.
    bool sliding{false};   ///< True when a momentum slide is active (crouch at speed).
};
