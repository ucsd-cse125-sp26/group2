/// @file HostConfigUI.hpp
/// @brief ImGui widget for local server hosting controls.

#pragma once

#include "host/HostedServer.hpp"

#include <cstdint>
#include <string_view>

/// @brief Inputs needed to render the host-configuration UI for one frame.
struct HostConfigUIInputs
{
    HostConfigState& draft;               ///< Mutable draft settings bound to the widgets.
    bool serverRunning = false;           ///< True if the client is connected to a server session.
    bool canManageServer = false;         ///< True if the local client is authorized to manage server settings.
    bool ownsLocalProcess = false;        ///< True if this app still owns the hosted child process.
    bool hasUnsavedServerChanges = false; ///< True if draft settings differ from server state.
    uint16_t boundPort = 0;               ///< Actual bound server port when running.
    std::string_view errorMessage;        ///< Last launch error, empty when none.
};

/// @brief User actions emitted by the host-configuration UI for one frame.
struct HostConfigResult
{
    bool launchClicked = false;     ///< True if the user pressed "Launch" this frame.
    bool updateClicked = false;     ///< True if the user pressed "Update Settings" this frame.
    bool shutdownClicked = false;   ///< True if the user pressed "Shutdown" this frame.
    bool goToLobbyClicked = false;  ///< True if the user wants to enter the lobby.
    bool backToHomeClicked = false; ///< True if the user wants to return to the main menu.
};

namespace host_config_ui
{

/// @brief Render the host-configuration window and return requested actions.
/// @param inputs Current host settings and hosted-server status.
/// @return Buttons/actions selected by the user this frame.
HostConfigResult buildHostConfigMenu(const HostConfigUIInputs& inputs);
} // namespace host_config_ui
