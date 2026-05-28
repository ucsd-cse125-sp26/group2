/// @file HostConfigUI.cpp
/// @brief ImGui implementation of local server hosting controls.

#include "HostConfigUI.hpp"

#include "network/ServerName.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace host_config_ui
{
HostConfigResult buildHostConfigMenu(const HostConfigUIInputs& inputs)
{
    HostConfigResult result{};
    HostConfigState& draft = inputs.draft;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(480.0f, 360.0f), ImGuiCond_Once);
    if (ImGui::Begin("Host Game")) {
        ImGui::SeparatorText("Settings");
        if (ImGui::BeginTable("HostSettings", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Server Name");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(inputs.serverRunning);
            ImGui::SetNextItemWidth(240.0f);
            ImGui::InputText("##ServerName", &draft.serverName);
            draft.serverName = server_name::clampUtf8Bytes(draft.serverName);
            ImGui::EndDisabled();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Kill Threshold to Win");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderInt("##KillsToWin", &draft.killsToWin, 1, 100);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Max Players");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::SliderInt("##MaxPlayers", &draft.maxPlayers, 2, 128);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Keep Server Running");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(inputs.serverRunning);
            ImGui::Checkbox("##PersistentServer", &draft.persistAfterClientExit);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("stays online after you close the game");

            ImGui::EndTable();
        }

        ImGui::SeparatorText("Advanced");
        if (ImGui::BeginTable("HostAdvancedSettings", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Specific Port");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(inputs.serverRunning);
            ImGui::Checkbox("##UseSpecificPort", &draft.useSpecificPort);
            if (draft.useSpecificPort) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                ImGui::InputInt("##HostPort", &draft.port);
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("auto");
            }
            ImGui::EndDisabled();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Transport");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(inputs.serverRunning);
            ImGui::Checkbox("Legacy TCP", &draft.useLegacyTcp);
            ImGui::EndDisabled();
            if (draft.useLegacyTcp && !draft.useSpecificPort) {
                ImGui::SameLine();
                ImGui::TextDisabled("requires a specific port");
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Advertise on LAN");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(inputs.serverRunning && !inputs.canManageServer);
            ImGui::Checkbox("##AdvertiseLan", &draft.advertiseLan);
            ImGui::EndDisabled();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Advertise on Internet");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(inputs.serverRunning && !inputs.canManageServer);
            ImGui::Checkbox("##AdvertiseGlobal", &draft.advertiseGlobal);
            ImGui::EndDisabled();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Auto Port");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(draft.useSpecificPort ? "Off" : "On");
            ImGui::SameLine();
            ImGui::TextDisabled("UDP session only");

            ImGui::EndTable();
        }

        ImGui::SeparatorText("Server");
        if (inputs.serverRunning) {
            if (inputs.boundPort != 0) {
                ImGui::Text("Connected on port %u", static_cast<unsigned>(inputs.boundPort));
            } else {
                ImGui::TextUnformatted("Connected to server");
            }
            if (inputs.ownsLocalProcess) {
                ImGui::SameLine();
                ImGui::TextDisabled("local process");
            }
            if (inputs.hasUnsavedServerChanges) {
                ImGui::SameLine();
                ImGui::TextDisabled("Unsaved changes");
            }
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
            ImGui::BeginDisabled(!inputs.canManageServer || !inputs.hasUnsavedServerChanges);
            if (ImGui::Button("Update Settings")) {
                result.updateClicked = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Go to Lobby")) {
                result.goToLobbyClicked = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!inputs.canManageServer);
            if (ImGui::Button("Shutdown")) {
                result.shutdownClicked = true;
            }
            ImGui::EndDisabled();
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
