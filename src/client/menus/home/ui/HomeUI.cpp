#include "HomeUI.hpp"

#include <imgui.h>

namespace home_ui
{
bool buildJoinMenu()
{
    static char server_ip[64] = "127.0.0.1";
    static int server_port = 8080;
    bool connectClicked = false;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f), ImGuiCond_Once);
    if (ImGui::Begin("Join Game")) {
        ImGui::InputText("Server IP", server_ip, IM_ARRAYSIZE(server_ip));
        ImGui::InputInt("Port", &server_port);
    }
    if (ImGui::Button("Join")) {
        connectClicked = true;
    }
    ImGui::End();

    return connectClicked;
}
} // namespace home_ui
