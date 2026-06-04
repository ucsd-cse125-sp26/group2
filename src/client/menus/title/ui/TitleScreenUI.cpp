/// @file TitleScreenUI.cpp
/// @brief ImGui implementation of the top-level landing menu.

#include "TitleScreenUI.hpp"

#include "menus/MenuTheme.hpp"

#include <imgui.h>

namespace title_screen_ui
{
TitleScreenResult buildTitleScreen()
{
    TitleScreenResult result{};

    if (menu_theme::beginPanel("Metal: Orbital Arena", 560.0f, 430.0f, true)) {
        menu_theme::terminalStatusLine("SYSTEM BOOT: ARENA FRONTEND", "NAV: ARROWS / ENTER");
        menu_theme::terminalSection("COMMANDS");
        const ImVec2 rowSize(0.0f, 38.0f);

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        if (menu_theme::terminalActionRow("PLAY", "open server browser", rowSize)) {
            result.playClicked = true;
        }
        if (menu_theme::terminalActionRow("HOST", "configure local server", rowSize)) {
            result.hostClicked = true;
        }
        if (menu_theme::terminalActionRow("SETTINGS", "controls and video feel", rowSize)) {
            result.settingsClicked = true;
        }
        if (menu_theme::terminalActionRow("EXIT", "close application", rowSize, true)) {
            result.exitClicked = true;
        }

        menu_theme::terminalStatusLine("PROMPT READY", "BUILD group2");
    }
    menu_theme::endPanel();

    return result;
}
} // namespace title_screen_ui
