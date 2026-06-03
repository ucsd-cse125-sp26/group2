/// @file MainMenuUI.hpp
/// @brief ImGui widget for the top-level landing menu.

#pragma once

/// @brief Output from a single main-menu UI frame.
struct MainMenuResult
{
    bool playClicked = false; ///< True if the user pressed "Play" this frame.
    bool hostClicked = false; ///< True if the user pressed "Host" this frame.
    bool exitClicked = false; ///< True if the user pressed "Exit" this frame.
};

namespace main_menu_ui
{
/// @brief Render the landing menu and return any user action this frame.
MainMenuResult buildMainMenu();
} // namespace main_menu_ui
