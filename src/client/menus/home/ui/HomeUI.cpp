/// @file HomeUI.cpp
/// @brief ImGui implementation of the server join form widget.

#include "HomeUI.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace home_ui
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

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(620.0f, 420.0f), ImGuiCond_Once);
    if (ImGui::Begin("Join Game")) {
        ImGui::InputText("Server IP", &state.serverIp);
        ImGui::InputInt("Port", &state.serverPort);
        if (ImGui::Button("Join")) {
            result.connectClicked = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Host")) {
            result.hostClicked = true;
        }
        if (!errorMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(1.0f, 0.25f, 0.25f, 1.0f), "%.*s", static_cast<int>(errorMessage.size()), errorMessage.data());
        }

        ImGui::SeparatorText("Local Servers");
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
                if (ImGui::SmallButton("Join")) {
                    result.localServerIndex = i;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::SeparatorText("Global Servers");
        if (ImGui::Button(browserRefreshing ? "Refreshing..." : "Refresh")) {
            result.refreshClicked = true;
        }
        if (!browserError.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(
                ImVec4(1.0f, 0.45f, 0.2f, 1.0f), "%.*s", static_cast<int>(browserError.size()), browserError.data());
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
                ImGui::Text("%s:%u", server.host.c_str(), server.gamePort);
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
                if (ImGui::SmallButton("Join")) {
                    result.globalServerIndex = i;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();

    return result;
}
} // namespace home_ui
