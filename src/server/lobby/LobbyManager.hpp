/// @file LobbyManager.hpp
/// @brief Server-side lobby state machine: player join/leave, ready-up, and match-start gating.

#pragma once

#include "network/Server.hpp"
#include "network/lobby/LobbyStatus.hpp"

#include <chrono>
#include <unordered_map>

/// @brief Manages authoritative lobby state and broadcasts updates to connected clients.
///
/// Tracks the player roster, host assignment, and ready statuses.  All mutating
/// operations broadcast the appropriate LobbyUpdateEvent via the Server.
class LobbyManager
{
public:
    /// @brief Bind the server reference used for all broadcasts.
    bool init(Server& serverPtr);

    /// @brief Register a newly connected player and broadcast a PlayerJoined event.
    /// @return False if the player was already present.
    bool addPlayer(ClientId id);

    /// @brief Unregister a disconnected player, broadcast PlayerLeft, and reassign host if needed.
    /// @return False if the player was not found.
    bool removePlayer(ClientId id);

    /// @brief Update a player's ready flag and broadcast the corresponding Ready/Unready event.
    /// @return False if the player was not found.
    bool setPlayerReadyStatus(ClientId id, bool ready);

    /// @brief Validate a host-initiated match start request.
    ///
    /// Rejects if sender is not the host or any connected non-host player is unready.
    /// Host-only lobbies are valid.
    /// @return True if the match may proceed.
    bool hostStartMatch(ClientId sender);

    /// @brief Clear all ready flags and broadcast Unready events; resends full lobby state to every client.
    void resetReadyStatuses();

private:
    Server* server = nullptr;         ///< Authoritative server; not owned.
    std::vector<LobbyPlayer> players; ///< Current player roster.
    std::unordered_map<ClientId, std::chrono::steady_clock::time_point>
        joinTimes;                    ///< Join timestamps, used for host re-election.
    ClientId hostId{-1};              ///< ID of the current host; -1 if lobby is empty.

    /// @brief Elect the longest-standing player as host and broadcast PlayerNewHost.
    ClientId assignNewHost();

    /// @brief Send a full LobbyState snapshot to every player in the roster.
    void sendLobbyStateToAllPlayers();
};
