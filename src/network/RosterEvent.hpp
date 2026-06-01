#pragma once

#include "ecs/components/ClientId.hpp"

#include <cstdint>

/// @brief Type of mid-match roster change replicated to active clients.
enum class RosterEventType : std::uint8_t
{
    PlayerJoined, ///< A player connected after the lobby phase.
    PlayerLeft    ///< A player left or disconnected after the lobby phase.
};

/// @brief Server-authored join/leave notification for in-progress matches.
///
/// Sent as the payload of @ref PacketType::ROSTER_UPDATE. The fixed-size name
/// keeps the packet trivially copyable; clients fall back to @ref ClientId if
/// the name is empty or the replicated entity has already disappeared.
struct PlayerRosterEvent
{
    RosterEventType type = RosterEventType::PlayerJoined; ///< Join/leave discriminator.
    ClientId id{-1};                                      ///< Client that joined or left.
    char name[32] = {};                                   ///< Null-terminated display name when available.
};
