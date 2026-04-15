/// @file Event.hpp
/// @brief Client Event structure to be consumed by server game loop.
#pragma once
#include "EventType.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/InputSnapshot.hpp"

/// @brief A single gameplay event produced by network input processing.
class Event
{
public:
    ClientId clientId;                 ///< Originating client identifier.
    EventType type;
    InputSnapshot movementIntent = {}; ///< Decoded movement fields.
};