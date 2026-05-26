#include "HostConfigUI.hpp"

#include <imgui.h>

namespace host_config_ui
{
HostConfigResult buildHostConfigMenu(const HostConfigUIInputs& inputs)
{
    HostConfigResult result{};
    HostConfigState& draft = inputs.draft;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 300.0f), ImGuiCond_Once);
    if (ImGui::Begin("Host Game")) {
        ImGui::SeparatorText("Settings");
        if (ImGui::BeginTable("HostSettings", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Port");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(inputs.serverRunning);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("##HostPort", &draft.port);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("0 = auto");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Persistent Server");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(inputs.serverRunning);
            ImGui::Checkbox("##PersistentServer", &draft.persistAfterClientExit);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("not yet wired");

            ImGui::EndTable();
        }

        ImGui::SeparatorText("Server");
        if (inputs.serverRunning) {
            ImGui::Text("Running on port %u", static_cast<unsigned>(inputs.boundPort));
        } else {
            ImGui::TextUnformatted("Not running");
        }

        if (!inputs.errorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
                               "%.*s",
                               static_cast<int>(inputs.errorMessage.size()),
                               inputs.errorMessage.data());
        }

        ImGui::Spacing();
        ImGui::BeginDisabled(inputs.serverRunning);
        if (ImGui::Button("Launch Server")) {
            result.launchClicked = true;
        }
        ImGui::EndDisabled();

        if (inputs.serverRunning) {
            ImGui::SameLine();
            if (ImGui::Button("Go to Lobby")) {
                result.goToLobbyClicked = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Shutdown")) {
                result.shutdownClicked = true;
            }
        }

        ImGui::Separator();
        ImGui::BeginDisabled(inputs.serverRunning);
        if (ImGui::Button("Back to Main Menu")) {
            result.backToHomeClicked = true;
        }
        ImGui::EndDisabled();
    }
    ImGui::End();

    return result;
}
} // namespace host_config_ui
