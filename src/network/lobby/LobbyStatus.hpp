#pragma once
#include "ecs/components/ClientId.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct LobbyPlayer
{
    ClientId id;
    bool ready = false;
    bool isHost = false;
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
        PlayerNewHost = 3,
    };

    Type type;
    ClientId id;
};
