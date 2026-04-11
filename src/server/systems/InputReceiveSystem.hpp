#pragma once

/// @brief Deserialises incoming client InputSnapshot packets and writes them into the registry (not yet implemented).
#include "ecs/components/InputSnapshot.hpp"
#include "systems/EventQueue.hpp"
namespace systems
{

inline Event runInputReceive(const void* data)
{
    auto* snap = static_cast<const InputSnapshot*>(data);
    Event event;

    // Movement
    event.movementIntent.forward = snap->forward;
    event.movementIntent.back = snap->back;
    event.movementIntent.left = snap->left;
    event.movementIntent.right = snap->right;
    event.movementIntent.jump = snap->jump;
    event.movementIntent.crouch = snap->crouch;
    event.movementIntent.yaw = snap->yaw;
    event.movementIntent.pitch = snap->pitch;
    event.movementIntent.roll = snap->roll;

    // Action
    event.shootIntent = snap->shooting;

    return event;
}

} // namespace systems
