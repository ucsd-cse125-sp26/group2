/// @file HomeUI.hpp
/// @brief ImGui widget for the main menu server join form.

#pragma once

#include <string>
#include <string_view>

/// @brief Mutable widget state for the server join form.
struct JoinMenuState
{
    std::string serverIp = "127.0.0.1"; ///< Server hostname or IP address entered by the user.
    int serverPort = 9999;              ///< Server port entered by the user.
};

/// @brief Output from a single home UI frame.
struct JoinMenuResult
{
    bool connectClicked = false; ///< True if the user pressed "Join" this frame.
};

namespace home_ui
{

/// @brief Render the join game window and return any user action this frame.
/// @param state      Persistent widget state (IP/port fields).
/// @param errorMessage Optional error string displayed in red below the form.
/// @return Actions the caller should apply (connect request).
JoinMenuResult buildJoinMenu(JoinMenuState& state, std::string_view errorMessage = {});

} // namespace home_ui
