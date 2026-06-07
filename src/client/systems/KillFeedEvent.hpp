/// @file KillEvent.hpp
/// @brief Local definition of kill event structu
#pragma once

#include "ecs/components/ClientId.hpp"

struct KillFeedEvent
{
    ClientId killerId;         // ID of the player who made the kill
    ClientId victimId;         // ID of the player killed
    int weaponId = -1;         // WeaponType ordinal, or -1 for environment/unknown deaths
    bool isHeadshot = false;   // True when the killing blow hit the head
    float displayTimer = 5.0f; // Timer for how long to display the kill feed entry
    bool sentToHud = false;    // Set true after first HUD pass to prevent duplicates
};
