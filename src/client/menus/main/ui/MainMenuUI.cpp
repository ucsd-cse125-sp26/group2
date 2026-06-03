/// @file MainMenuUI.cpp
/// @brief ImGui implementation of the top-level landing menu.

#include "MainMenuUI.hpp"

#include "menus/MenuTheme.hpp"

#include <imgui.h>

namespace main_menu_ui
{
MainMenuResult buildMainMenu()
{
    MainMenuResult result{};

    if (menu_theme::beginPanel("Metal: Orbital Arena", 460.0f, 440.0f, true)) {
        ImGui::Spacing();
        const float buttonWidth = ImGui::GetContentRegionAvail().x;
        const ImVec2 buttonSize(buttonWidth, 44.0f);

        if (menu_theme::accentButton("Play", buttonSize)) {
            result.playClicked = true;
        }
        if (ImGui::Button("Host", buttonSize)) {
            result.hostClicked = true;
        }
        ImGui::BeginDisabled();
        ImGui::Button("Settings", buttonSize);
        ImGui::EndDisabled();
        if (menu_theme::dangerButton("Exit", buttonSize)) {
            result.exitClicked = true;
        }
    }
    menu_theme::endPanel();

    return result;
}
} // namespace main_menu_ui
