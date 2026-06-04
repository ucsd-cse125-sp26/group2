/// @file MainMenuUI.cpp
/// @brief ImGui implementation of the server join form widget.

#include "MainMenuUI.hpp"

#include "menus/MenuTheme.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace main_menu_ui
{
JoinMenuResult buildJoinMenu(JoinMenuState& state,
                             std::string_view errorMessage,
                             const std::vector<DiscoveryClient::DiscoveredServer>& localServers,
                             const std::vector<net::discovery::ServerInfo>& globalServers,
                             std::string_view browserError,
                             bool browserRefreshing)
{
    JoinMenuResult result;
    result.connectClicked = false;

    if (menu_theme::beginPanel("Join Game", 760.0f, 560.0f, true)) {
        menu_theme::terminalStatusLine("NETSCAN READY", "TYPE ADDRESS OR SELECT HOST");
        menu_theme::terminalSection("DIRECT CONNECT");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        ImGui::InputText("Server IP", &state.serverIp);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        ImGui::InputInt("Port", &state.serverPort);

        const ImVec2 actionSize(0.0f, 32.0f);
        if (menu_theme::terminalActionRow("JOIN", "connect to entered address", actionSize)) {
            result.connectClicked = true;
        }
        if (menu_theme::terminalActionRow("HOST", "open local server config", actionSize)) {
            result.hostClicked = true;
        }
        if (!errorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(menu_theme::settings().dangerActive,
                               "%.*s",
                               static_cast<int>(errorMessage.size()),
                               errorMessage.data());
        }

        menu_theme::terminalSection("LOCAL SERVERS");
        if (ImGui::BeginTable("LocalServerTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Address");
            ImGui::TableSetupColumn("Players");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(localServers.size()); ++i) {
                const auto& server = localServers[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(server.serverName.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s:%u", server.hostIp.c_str(), server.gamePort);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u/%u", server.currentPlayers, server.maxPlayers);
                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(i);
                const bool lobbyFull = server.maxPlayers != 0 && server.currentPlayers >= server.maxPlayers;
                ImGui::BeginDisabled(lobbyFull);
                if (menu_theme::terminalActionRow("CONNECT", nullptr, ImVec2(0.0f, 0.0f))) {
                    result.localServerIndex = i;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        menu_theme::terminalSection("GLOBAL SERVERS");
        if (menu_theme::terminalActionRow(
                browserRefreshing ? "REFRESHING..." : "REFRESH", "query directory", ImVec2(0.0f, 30.0f)))
        {
            result.refreshClicked = true;
        }
        if (!browserError.empty()) {
            ImGui::TextColored(
                menu_theme::settings().textDim, "%.*s", static_cast<int>(browserError.size()), browserError.data());
        }

        if (ImGui::BeginTable("GlobalServerTable", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Address");
            ImGui::TableSetupColumn("Players");
            ImGui::TableSetupColumn("NAT");
            ImGui::TableSetupColumn("Seen");
            ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(globalServers.size()); ++i) {
                const auto& server = globalServers[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(server.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s:%u",
                            server.udpHost.empty() ? server.host.c_str() : server.udpHost.c_str(),
                            server.udpPort != 0 ? server.udpPort : server.gamePort);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%u/%u", server.currentPlayers, server.maxPlayers);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(server.natTraversalReady ? "assist" : "direct");
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%llums", static_cast<unsigned long long>(server.lastSeenMs));
                ImGui::TableSetColumnIndex(5);
                ImGui::PushID(i);
                const bool lobbyFull = server.maxPlayers != 0 && server.currentPlayers >= server.maxPlayers;
                ImGui::BeginDisabled(lobbyFull);
                if (menu_theme::terminalActionRow("CONNECT", nullptr, ImVec2(0.0f, 0.0f))) {
                    result.globalServerIndex = i;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        menu_theme::terminalStatusLine("BROWSER ONLINE", "ESC: SYSTEM MENU");
        if (menu_theme::terminalActionRow("BACK", "return to title screen", ImVec2(0.0f, 32.0f))) {
            result.returnToTitleScreenClicked = true;
        }
    }
    menu_theme::endPanel();

    return result;
}
} // namespace main_menu_ui
