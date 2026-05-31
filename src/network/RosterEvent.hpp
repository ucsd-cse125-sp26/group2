#pragma once

#include "ecs/components/ClientId.hpp"

#include <cstdint>

enum class RosterEventType : std::uint8_t
{
    PlayerJoined,
    PlayerLeft
};

struct PlayerRosterEvent
{
    RosterEventType type = RosterEventType::PlayerJoined;
    ClientId id{-1};
    char name[32] = {};
};
