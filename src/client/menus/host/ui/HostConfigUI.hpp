#pragma once

#include "host/HostedServer.hpp"

#include <cstdint>
#include <string_view>

struct HostConfigUIInputs
{
    HostConfigState& draft;        ///< Mutable draft settings bound to the widgets.
    bool serverRunning = false;    ///< True if the hosted server process is alive.
    uint16_t boundPort = 0;        ///< Actual bound server port when running.
    std::string_view errorMessage; ///< Last launch error, empty when none.
};

struct HostConfigResult
{
    bool launchClicked = false;     ///< True if the user pressed "Launch" this frame.
    bool shutdownClicked = false;   ///< True if the user pressed "Shutdown" this frame.
    bool goToLobbyClicked = false;  ///< True if the user wants to enter the lobby.
    bool backToHomeClicked = false; ///< True if the user wants to return to the main menu.
};

namespace host_config_ui
{
HostConfigResult buildHostConfigMenu(const HostConfigUIInputs& inputs);
} // namespace host_config_ui
