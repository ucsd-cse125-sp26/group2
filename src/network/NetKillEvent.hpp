/// @file NetKillEvent.hpp
/// @brief Structure of kill event broadcasted from server to clients
#pragma once

#include "ecs/components/ClientId.hpp"
#include "ecs/components/Health.hpp"
#include "ecs/components/Hitbox.hpp"

/// @brief Event representing a player kill, sent from server to client for
/// kill feed updates.
struct NetKillEvent
{
    ClientId killerId;                             // ID of the player who made the kill
    ClientId victimId;                             // ID of the player killed
    Health killerHealth;                           // Remaining health of killer for respawn display
    int weaponId = -1;                             // WeaponType ordinal, or -1 for environment/unknown deaths
    BodyRegion hitRegion = BodyRegion::UpperTorso; // Body region of the killing blow
    bool isHeadshot = false;                       // Convenience flag for UI (region == Head)
};
