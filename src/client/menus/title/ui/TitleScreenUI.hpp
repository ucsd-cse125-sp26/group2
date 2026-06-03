/// @file TitleScreenUI.hpp
/// @brief ImGui widget for the top-level landing menu.

#pragma once

/// @brief Output from a single title-screen UI frame.
struct TitleScreenResult
{
    bool playClicked = false; ///< True if the user pressed "Play" this frame.
    bool hostClicked = false; ///< True if the user pressed "Host" this frame.
    bool exitClicked = false; ///< True if the user pressed "Exit" this frame.
};

namespace title_screen_ui
{
/// @brief Render the landing menu and return any user action this frame.
TitleScreenResult buildTitleScreen();
} // namespace title_screen_ui
