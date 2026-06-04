/// @file TitleScreenUI.cpp
/// @brief ImGui implementation of the top-level landing menu.

#include "TitleScreenUI.hpp"

#include "menus/MenuTheme.hpp"

#include <algorithm>
#include <imgui.h>

namespace title_screen_ui
{
TitleScreenResult buildTitleScreen()
{
    TitleScreenResult result{};

    if (menu_theme::beginPanel(
            "Metal: Orbital Arena", menu_theme::k_frontendPanelBaseWidth, menu_theme::k_frontendPanelBaseHeight, true))
    {
        menu_theme::terminalStatusLine("SYSTEM BOOT: ARENA FRONTEND", "NAV: ARROWS / ENTER");
        menu_theme::terminalSection("COMMANDS");

        const float displayW = ImGui::GetIO().DisplaySize.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float buttonW = std::min(displayW * 0.2f, avail);
        const ImVec2 rowSize(buttonW, 38.0f);

        auto drawRow = [&](const char* label, const char* desc, bool danger) {
            return menu_theme::terminalActionRow(label, desc, rowSize, danger);
        };

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        if (drawRow("PLAY", nullptr, false)) {
            result.playClicked = true;
        }
        if (drawRow("HOST", nullptr, false)) {
            result.hostClicked = true;
        }
        if (drawRow("SETTINGS", nullptr, false)) {
            result.settingsClicked = true;
        }
        if (drawRow("EXIT", nullptr, true)) {
            result.exitClicked = true;
        }

        menu_theme::terminalStatusLine("PROMPT READY", "BUILD group2");
    }
    menu_theme::endPanel();

    return result;
}
} // namespace title_screen_ui
