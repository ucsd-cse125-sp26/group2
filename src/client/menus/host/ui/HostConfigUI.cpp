/// @file HostConfigUI.cpp
/// @brief ImGui implementation of local server hosting controls.

#include "HostConfigUI.hpp"

#include "menus/MenuTheme.hpp"
#include "network/ServerName.hpp"

#include <cstdio>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace host_config_ui
{
namespace
{
constexpr float k_labelColumnWidth = 250.0f;
constexpr float k_portInputWidth = 220.0f;
} // namespace

HostConfigResult buildHostConfigContents(const HostConfigUIInputs& inputs, bool showBackRow)
{
    HostConfigResult result{};
    HostConfigState& draft = inputs.draft;

    char statusLeft[96];
    if (inputs.serverRunning && inputs.boundPort != 0) {
        std::snprintf(statusLeft,
                      sizeof(statusLeft),
                      "SERVER SESSION ACTIVE ON PORT %u",
                      static_cast<unsigned>(inputs.boundPort));
    } else if (inputs.serverRunning) {
        std::snprintf(statusLeft, sizeof(statusLeft), "SERVER SESSION ACTIVE");
    } else {
        std::snprintf(statusLeft, sizeof(statusLeft), "SERVER OFFLINE");
    }
    menu_theme::terminalStatusLine(statusLeft, inputs.hasUnsavedServerChanges ? "UNSAVED CHANGES" : "CONFIG CLEAN");

    const float spacingY = ImGui::GetStyle().ItemSpacing.y;
    const int footerRows = showBackRow ? 2 : 1;
    const float trailingStatusHeight = showBackRow ? ImGui::GetTextLineHeightWithSpacing() : 0.0f;
    const float footerHeight =
        static_cast<float>(footerRows) * 32.0f + spacingY * static_cast<float>(footerRows + 2) + trailingStatusHeight;

    menu_theme::beginScrollBody("##HostConfigBody", footerHeight);
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
        if (ImGui::InputText("##ServerName", &draft.serverName)) {
            menu_theme::playUiSound(UiSoundAction::SliderStep);
        }
        draft.serverName = server_name::clampUtf8Bytes(draft.serverName);
        ImGui::EndDisabled();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Kill Threshold to Win");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderInt("##KillsToWin", &draft.killsToWin, 1, 100)) {
            menu_theme::playUiSound(UiSoundAction::SliderStep);
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Max Players");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderInt("##MaxPlayers", &draft.maxPlayers, 2, 128)) {
            menu_theme::playUiSound(UiSoundAction::SliderStep);
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Keep Server Running");
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(inputs.serverRunning);
        if (ImGui::Checkbox("##PersistentServer", &draft.persistAfterClientExit)) {
            menu_theme::playUiSound(UiSoundAction::Toggle);
        }
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
        if (ImGui::Checkbox("##UseSpecificPort", &draft.useSpecificPort)) {
            menu_theme::playUiSound(UiSoundAction::Toggle);
        }
        if (draft.useSpecificPort) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(k_portInputWidth);
            if (ImGui::InputInt("##HostPort", &draft.port)) {
                menu_theme::playUiSound(UiSoundAction::SliderStep);
            }
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
        if (ImGui::Checkbox("Legacy TCP", &draft.useLegacyTcp)) {
            menu_theme::playUiSound(UiSoundAction::Toggle);
        }
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
        if (ImGui::Checkbox("##AdvertiseLan", &draft.advertiseLan)) {
            menu_theme::playUiSound(UiSoundAction::Toggle);
        }
        ImGui::EndDisabled();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Advertise on Internet");
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(inputs.serverRunning && !inputs.canManageServer);
        if (ImGui::Checkbox("##AdvertiseGlobal", &draft.advertiseGlobal)) {
            menu_theme::playUiSound(UiSoundAction::Toggle);
        }
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

    if (!inputs.errorMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(menu_theme::settings().dangerActive,
                           "%.*s",
                           static_cast<int>(inputs.errorMessage.size()),
                           inputs.errorMessage.data());
    }
    menu_theme::endScrollBody();

    ImGui::Spacing();
    // Scale row height with the current font so buttons remain centered on
    // high-DPI / larger panels where SetWindowFontScale enlarges glyphs.
    const float btnHeight = std::max(32.0f, ImGui::GetFontSize() + 16.0f);
    {
        const float fullW = ImGui::GetContentRegionAvail().x;
        const float spacingX = ImGui::GetStyle().ItemSpacing.x;
        const float colW = (fullW - spacingX * 3.0f) / 4.0f;
        const ImVec2 btnSize(colW, btnHeight);

        ImGui::BeginDisabled(inputs.serverRunning);
        if (menu_theme::terminalActionRow("LAUNCH", nullptr, btnSize)) {
            result.launchClicked = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!inputs.serverRunning || !inputs.canManageServer || !inputs.hasUnsavedServerChanges);
        if (menu_theme::terminalActionRow("UPDATE", nullptr, btnSize)) {
            result.updateClicked = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!inputs.serverRunning);
        if (menu_theme::terminalActionRow("GO TO LOBBY", nullptr, btnSize)) {
            result.goToLobbyClicked = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!inputs.serverRunning || !inputs.canManageServer);
        if (menu_theme::terminalActionRow("SHUTDOWN", nullptr, btnSize, true)) {
            result.shutdownClicked = true;
        }
        ImGui::EndDisabled();
    }

    if (showBackRow) {
        menu_theme::terminalStatusLine("HOST CONFIG READY", "ARROWS / ENTER");
        ImGui::BeginDisabled(inputs.serverRunning);
        if (menu_theme::terminalActionRow("BACK", "return to main menu", ImVec2(0.0f, btnHeight))) {
            result.backToMainMenuClicked = true;
        }
        ImGui::EndDisabled();
    }

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
