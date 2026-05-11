#pragma once
#include "ecs/components/ClientId.hpp"

#include <string>

struct LobbyPlayer
{
    ClientId id;
    bool ready = false;
};

/// @brief Status of lobby. Sent to client upon joining.
struct LobbyState
{
    uint32_t count;
    std::vector<LobbyPlayer> players;
};

struct LobbyUpdateEvent
{
    enum class Type : uint8_t
    {
        PlayerJoined = 0,
        PlayerLeft = 1,
        PlayerReadyStatusChanged = 2,
    };

    Type type;
    ClientId id;
};
