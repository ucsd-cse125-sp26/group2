#include "HomeUI.hpp"

#include <imgui.h>

namespace home_ui
{
JoinMenuResult buildJoinMenu(JoinMenuState& state)
{
    // TODO: Display joinError in UI if set
    JoinMenuResult result;
    result.connectClicked = false;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f), ImGuiCond_Once);
    if (ImGui::Begin("Join Game")) {
        ImGui::InputText("Server IP", state.serverIp, IM_ARRAYSIZE(state.serverIp));
        ImGui::InputInt("Port", &state.serverPort);
        if (ImGui::Button("Join")) {
            result.connectClicked = true;
        }
    }
    ImGui::End();

    return result;
}
} // namespace home_ui
