#pragma once

// Locomotion state flags for a player entity.
// Read by MovementSystem to select the correct physics constants,
// and written by CollisionSystem (grounded) and MovementSystem (crouching/sliding).
struct PlayerState
{
    bool grounded{false};  // touching a floor surface this tick
    bool crouching{false}; // crouch input held; CollisionShape.halfExtents.y is reduced
    bool sliding{false};   // momentum slide active (crouch at speed)
};
