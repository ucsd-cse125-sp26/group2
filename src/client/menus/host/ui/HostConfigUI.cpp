/// @file HostConfigUI.cpp
/// @brief ImGui implementation of local server hosting controls.

#include "HostConfigUI.hpp"

#include "menus/MenuTheme.hpp"
#include "network/ServerName.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace host_config_ui
{
namespace
{
constexpr float k_labelColumnWidth = 190.0f;
}

HostConfigResult buildHostConfigContents(const HostConfigUIInputs& inputs)
{
    HostConfigResult result{};
    HostConfigState& draft = inputs.draft;

    menu_theme::terminalStatusLine(inputs.serverRunning ? "SERVER SESSION ACTIVE" : "SERVER OFFLINE",
                                   inputs.hasUnsavedServerChanges ? "UNSAVED CHANGES" : "CONFIG CLEAN");
    menu_theme::terminalSection("SETTINGS");
    if (ImGui::BeginTable("HostSettings", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, k_labelColumnWidth);
        ImGui::TableSetupColumn("Value");

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Server Name");
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(inputs.serverRunning);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##ServerName", &draft.serverName);
        draft.serverName = server_name::clampUtf8Bytes(draft.serverName);
        ImGui::EndDisabled();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Kill Threshold to Win");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##KillsToWin", &draft.killsToWin, 1, 100);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Max Players");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##MaxPlayers", &draft.maxPlayers, 2, 128);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Keep Server Running");
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(inputs.serverRunning);
        ImGui::Checkbox("##PersistentServer", &draft.persistAfterClientExit);
        ImGui::EndDisabled();

        ImGui::EndTable();
    }

    menu_theme::terminalSection("ADVANCED");
    if (ImGui::BeginTable("HostAdvancedSettings", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, k_labelColumnWidth);
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

    menu_theme::terminalSection("SERVER");
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
        ImGui::TextColored(menu_theme::settings().dangerActive,
                           "%.*s",
                           static_cast<int>(inputs.errorMessage.size()),
                           inputs.errorMessage.data());
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(inputs.serverRunning);
    if (menu_theme::terminalActionRow("LAUNCH SERVER", "start local host process", ImVec2(0.0f, 32.0f))) {
        result.launchClicked = true;
    }
    ImGui::EndDisabled();

    if (inputs.serverRunning) {
        ImGui::BeginDisabled(!inputs.canManageServer || !inputs.hasUnsavedServerChanges);
        if (menu_theme::terminalActionRow("UPDATE SETTINGS", "push draft to server", ImVec2(0.0f, 32.0f))) {
            result.updateClicked = true;
        }
        ImGui::EndDisabled();
        if (menu_theme::terminalActionRow("GO TO LOBBY", "enter pre-match room", ImVec2(0.0f, 32.0f))) {
            result.goToLobbyClicked = true;
        }
        ImGui::BeginDisabled(!inputs.canManageServer);
        if (menu_theme::terminalActionRow("SHUTDOWN", "stop hosted server", ImVec2(0.0f, 32.0f), true)) {
            result.shutdownClicked = true;
        }
        ImGui::EndDisabled();
    }

    menu_theme::terminalStatusLine("HOST CONFIG READY", "ARROWS / ENTER");
    ImGui::BeginDisabled(inputs.serverRunning);
    if (menu_theme::terminalActionRow("BACK", "return to main menu", ImVec2(0.0f, 32.0f))) {
        result.backToMainMenuClicked = true;
    }
    ImGui::EndDisabled();

    return result;
}

HostConfigResult buildHostConfigMenu(const HostConfigUIInputs& inputs)
{
    HostConfigResult result{};
    if (menu_theme::beginPanel(
            "Host Game", menu_theme::k_frontendPanelBaseWidth, menu_theme::k_frontendPanelBaseHeight, true))
    {
        result = buildHostConfigContents(inputs);
    }
    menu_theme::endPanel();
    return result;
}
} // namespace host_config_ui
