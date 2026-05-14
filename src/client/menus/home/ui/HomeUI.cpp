/// @file HomeUI.cpp
/// @brief ImGui implementation of the server join form widget.

#include "HomeUI.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace home_ui
{
JoinMenuResult buildJoinMenu(JoinMenuState& state, std::string_view errorMessage)
{
    JoinMenuResult result;
    result.connectClicked = false;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f), ImGuiCond_Once);
    if (ImGui::Begin("Join Game")) {
        ImGui::InputText("Server IP", &state.serverIp);
        ImGui::InputInt("Port", &state.serverPort);
        if (ImGui::Button("Join")) {
            result.connectClicked = true;
        }
        if (!errorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "%.*s", static_cast<int>(errorMessage.size()), errorMessage.data());
        }
    }
    ImGui::End();

    return result;
}
} // namespace home_ui
