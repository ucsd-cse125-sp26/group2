/// @file LobbyUI.hpp
/// @brief ImGui widgets for the pre-match lobby player list and ready/start controls.

#pragma once
#include "network/lobby/LobbyStatus.hpp"

#include <optional>
#include <string_view>
#include <vector>

/// @brief Output from a single lobby UI frame.
struct BuildResult
{
    std::optional<bool>
        readyChange;                ///< Desired ready state change: true = ready, false = unready, nullopt = unchanged.
    bool startMatchClicked = false; ///< True if the host pressed "Start Match" this frame.
    bool returnToMenuClicked = false;       ///< True if the user pressed "Return to Main Menu" this frame.
    bool returnToHostConfigClicked = false; ///< True if the host wants to return to HostConfig without disconnecting.
};

/// @brief Input data consumed by lobby_ui::buildPlayerList each frame.
struct LobbyUIConfig
{
    const std::vector<LobbyPlayer>& players; ///< Current snapshot of all connected players.
    ClientId localId;                        ///< This client's own ID, used to label the local player.
    bool isHost;                             ///< True if the local client is the lobby host.
    bool canStartMatch;                      ///< True when the host is allowed to start the match.
    bool startCountdownActive;               ///< True while the pre-match countdown is running.
    float startCountdownRemaining;           ///< Seconds remaining in the countdown.
    bool isHosting;                          ///< True if this client owns a local hosted server process.
    std::string_view hostLanIp;              ///< LAN IPv4 shown to other local players.
    uint16_t hostPort;                       ///< Port shown to other players when hosting locally.
};

namespace lobby_ui
{

/// @brief Render the lobby player list window and return any player actions this frame.
/// @param config Read-only snapshot of lobby state for this frame.
/// @return Actions the caller should apply (ready toggle, start match, or navigation).
BuildResult buildPlayerList(const LobbyUIConfig& config);

} // namespace lobby_ui
