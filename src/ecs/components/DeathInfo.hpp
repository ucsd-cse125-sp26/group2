/// @file DeathInfo.hpp
/// @brief Component for enemy information upon death

#pragma once

#include "ecs/components/ClientId.hpp"
#include "ecs/components/Health.hpp"

struct DeathInfo
{
    ClientId killerId;   // ClientId of the killer
    Health killerHealth; // Health of the killer at the moment of the kill
};
