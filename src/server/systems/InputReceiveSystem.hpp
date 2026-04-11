/// @file InputReceiveSystem.hpp
/// @brief System for deserialising incoming client input packets into events.

#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "systems/EventQueue.hpp"

/// @brief Input receive system functions.
namespace systems
{

/// @brief Deserialise a raw InputSnapshot packet into a gameplay Event.
/// @param data Pointer to the raw InputSnapshot bytes.
/// @return An Event populated with the decoded movement and action intents.
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
