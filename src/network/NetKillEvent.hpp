/// @file NetKillEvent.hpp
/// @brief Structure of kill event broadcasted from server to clients
#pragma once

#include "ecs/components/ClientId.hpp"
#include "ecs/components/Health.hpp"

/// @brief Event representing a player kill, sent from server to client for
/// kill feed updates.
struct NetKillEvent
{
    ClientId killerId;   // ID of the player who made the kill
    ClientId victimId;   // ID of the player killed
    Health killerHealth; // Remaining health of killer for respawn display
};
