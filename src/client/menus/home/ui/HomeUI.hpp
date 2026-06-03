/// @file HomeUI.hpp
/// @brief ImGui widget for the main menu server join form.

#pragma once

#include "network/DiscoveryClient.hpp"
#include "network/discovery/GlobalDiscoveryProtocol.hpp"

#include <string>
#include <string_view>
#include <vector>

/// @brief Mutable widget state for the server join form.
struct JoinMenuState
{
    std::string serverIp = "127.0.0.1"; ///< Server hostname or IP address entered by the user.
    int serverPort = 2310;              ///< Server port entered by the user.
};

/// @brief Output from a single home UI frame.
struct JoinMenuResult
{
    bool connectClicked = false;      ///< True if the user pressed "Join" this frame.
    bool hostClicked = false;         ///< True if the user pressed "Host" this frame.
    bool refreshClicked = false;      ///< True if the user requested a global browser refresh.
    bool returnToMenuClicked = false; ///< True if the user pressed "Return to Menu" this frame.
    int localServerIndex = -1;        ///< Index of a discovered local server to join, or -1.
    int globalServerIndex = -1;       ///< Index of a discovered server to join, or -1.
};

namespace home_ui
{

/// @brief Render the join game window and return any user action this frame.
/// @param state Persistent widget state (IP/port fields).
/// @param errorMessage Optional error string displayed in red below the form.
/// @param localServers LAN-discovered servers shown in the local browser table.
/// @param globalServers Directory-discovered servers shown in the global browser table.
/// @param browserError Optional global-browser error displayed beside the refresh button.
/// @param browserRefreshing True while a global-browser refresh is in flight.
/// @return Actions the caller should apply (join, host, or refresh request).
JoinMenuResult buildJoinMenu(JoinMenuState& state,
                             std::string_view errorMessage,
                             const std::vector<DiscoveryClient::DiscoveredServer>& localServers,
                             const std::vector<net::discovery::ServerInfo>& globalServers,
                             std::string_view browserError,
                             bool browserRefreshing);

} // namespace home_ui
