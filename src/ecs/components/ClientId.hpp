/// @file ClientId.hpp
/// @brief Network client identifier component for multiplayer entities.

#pragma once

/// @brief Associates an entity with a connected network client.
struct ClientId
{
    int value = -1; ///< Network client ID. -1 = unassigned.
};
