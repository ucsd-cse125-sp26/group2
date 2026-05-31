/// @file LobbyUI.cpp
/// @brief ImGui implementation of the lobby player-list panel and ready/start controls.
#include "LobbyUI.hpp"

#include "menus/MenuTheme.hpp"

#include <imgui.h>

namespace lobby_ui
{

BuildResult buildPlayerList(const LobbyUIConfig& config)
{
    BuildResult result{};

    bool localReady = false;
    for (const auto& p : config.players) {
        if (p.id == config.localId) {
            localReady = p.ready;
            break;
        }
    }

    if (menu_theme::beginPanel("Lobby", 480.0f, 480.0f, true)) {
        if (!config.serverName.empty()) {
            ImGui::Text("Server: %.*s", static_cast<int>(config.serverName.size()), config.serverName.data());
            ImGui::Separator();
        }

        if (config.isHosting) {
            ImGui::SeparatorText("Hosting");
            ImGui::Text("Listen address: %.*s:%u",
                        static_cast<int>(config.hostLanIp.size()),
                        config.hostLanIp.data(),
                        static_cast<unsigned>(config.hostPort));
            ImGui::Text("Local address: 127.0.0.1:%u", static_cast<unsigned>(config.hostPort));
            ImGui::Spacing();
        }

        ImGui::Text("Players (%zu)", config.players.size());
        ImGui::Separator();
        for (const auto& p : config.players) {
            const bool isLocal = p.id == config.localId;
            if (p.isHost && isLocal)
                ImGui::Text("Player %d (Host, You)", p.id.value);
            else if (p.isHost)
                ImGui::Text("Player %d (Host)", p.id.value);
            else if (isLocal)
                ImGui::Text("Player %d (You)", p.id.value);
            else
                ImGui::Text("Player %d", p.id.value);

            ImGui::SameLine();
            if (p.ready)
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Ready");
            else
                ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.74f, 1.0f), "Not ready");
        }

        menu_theme::heading("Match Settings");
        if (config.matchConfig) {
            ImGui::Text("Kills to win: %d", config.matchConfig->killsToWin);
            ImGui::Text("Max players: %d", config.matchConfig->maxPlayers);
        } else {
            ImGui::TextDisabled("Waiting for match settings");
        }

        ImGui::Separator();
        if (config.startCountdownActive) {
            ImGui::Text("Entering match countdown in %.1fs", static_cast<double>(config.startCountdownRemaining));
        }

        ImGui::BeginDisabled(config.startCountdownActive);
        if (menu_theme::accentButton(localReady ? "Unready" : "Ready", ImVec2(150.0f, 0.0f))) {
            result.readyChange = !localReady;
        }

        if (config.isHost) {
            ImGui::SameLine();
            ImGui::BeginDisabled(!config.canStartMatch);
            if (menu_theme::accentButton("Start Match", ImVec2(150.0f, 0.0f))) {
                result.startMatchClicked = true;
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        if (config.isHost) {
            if (ImGui::Button("Back to Host Config")) {
                result.returnToHostConfigClicked = true;
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Return to Main Menu")) {
            result.returnToMenuClicked = true;
        }
    }
    menu_theme::endPanel();

    return result;
}

} // namespace lobby_ui
