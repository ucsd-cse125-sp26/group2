/// @file DeathInfo.hpp
/// @brief Component for enemy information upon death

#pragma once

#include "ecs/components/ClientId.hpp"
#include "ecs/components/Health.hpp"

/// @brief ECS component: information about the entity that killed this player.
///
/// Attached on death, removed on respawn.  Used by the client to display
/// the killer's health bar in the death screen.
struct DeathInfo
{
    ClientId killerId;   ///< Client ID of the killer.
    Health killerHealth; ///< Killer's health snapshot at time of the kill.
};
