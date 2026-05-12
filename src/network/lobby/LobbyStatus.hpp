/// @file LobbyStatus.hpp
/// @brief Shared lobby data types exchanged between server and clients.

#pragma once
#include "ecs/components/ClientId.hpp"

#include <cstdint>
#include <string>
#include <vector>

/// @brief Per-player state entry in the lobby.
struct LobbyPlayer
{
    ClientId id;         ///< Network identity of this player.
    bool ready = false;  ///< True when the player has confirmed they are ready to play.
    bool isHost = false; ///< True if this player is the current lobby host.
};

/// @brief Full lobby snapshot sent to a client upon joining.
struct LobbyState
{
    uint32_t count;                   ///< Number of players currently in the lobby.
    std::vector<LobbyPlayer> players; ///< Ordered list of all connected players.
};

/// @brief Incremental lobby change event broadcast to all connected clients.
struct LobbyUpdateEvent
{
    /// @brief Discriminator for the type of lobby change.
    enum class Type : uint8_t
    {
        PlayerJoined,  ///< A new player connected and entered the lobby.
        PlayerLeft,    ///< A player disconnected or left the lobby.
        PlayerReady,   ///< A player toggled their ready status to ready.
        PlayerUnready, ///< A player toggled their ready status to not ready.
        PlayerNewHost, ///< Host assignment transferred to a different player.
    };

    Type type;   ///< Kind of lobby change.
    ClientId id; ///< Player this event applies to.
};
