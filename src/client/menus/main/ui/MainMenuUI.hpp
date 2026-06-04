/// @file MainMenuUI.hpp
/// @brief ImGui widget for the main menu server join form.

#pragma once

#include "menus/host/ui/HostConfigUI.hpp"
#include "network/DiscoveryClient.hpp"
#include "network/discovery/GlobalDiscoveryProtocol.hpp"

#include <string>
#include <string_view>
#include <vector>

/// @brief Tabs available on the server browser screen.
enum class ServerBrowserTab
{
    LocalListing,
    GlobalListing,
    HostConfig,
};

/// @brief Mutable widget state for the server join form.
struct JoinMenuState
{
    std::string serverAddress;                                   ///< Server address entered by the user.
    ServerBrowserTab activeTab = ServerBrowserTab::LocalListing; ///< Currently selected browser tab.
    bool applyInitialTabSelection = true; ///< True when the active tab should be forced selected once.
    bool joining = false;                 ///< True while an outbound join attempt is in progress.
    std::string joiningLabel;             ///< Target label shown while joining.
};

/// @brief Output from a single main menu UI frame.
struct JoinMenuResult
{
    bool connectClicked = false;             ///< True if the user pressed "Join" this frame.
    bool localRefreshClicked = false;        ///< True if the user requested a LAN browser refresh.
    bool refreshClicked = false;             ///< True if the user requested a global browser refresh.
    bool returnToTitleScreenClicked = false; ///< True if the user pressed "Return to Title Screen" this frame.
    int localServerIndex = -1;               ///< Index of a discovered local server to join, or -1.
    int globalServerIndex = -1;              ///< Index of a discovered server to join, or -1.
    HostConfigResult hostConfig;             ///< Actions emitted by the host-config tab.
};

namespace main_menu_ui
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
                             bool browserRefreshing,
                             bool directConnectDisabled,
                             const HostConfigUIInputs& hostInputs);

} // namespace main_menu_ui
