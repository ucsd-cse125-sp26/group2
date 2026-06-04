/// @file LobbyUI.cpp
/// @brief ImGui implementation of the lobby player-list panel and ready/start controls.
#include "LobbyUI.hpp"

#include "menus/MenuTheme.hpp"

#include <cstdio>
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

    if (menu_theme::beginPanel(
            "Lobby", menu_theme::k_frontendPanelBaseWidth, menu_theme::k_frontendPanelBaseHeight, true))
    {
        menu_theme::terminalStatusLine(config.startCountdownActive ? "MATCH COUNTDOWN ACTIVE" : "WAITING FOR READY",
                                       config.isHost ? "HOST" : "CLIENT");
        if (!config.serverName.empty()) {
            ImGui::Text("Server: %.*s", static_cast<int>(config.serverName.size()), config.serverName.data());
        }

        if (config.isHosting) {
            menu_theme::terminalSection("HOSTING");
            ImGui::Text("Listen address: %.*s:%u",
                        static_cast<int>(config.hostLanIp.size()),
                        config.hostLanIp.data(),
                        static_cast<unsigned>(config.hostPort));
            ImGui::Text("Local address: 127.0.0.1:%u", static_cast<unsigned>(config.hostPort));
            ImGui::Spacing();
        }

        char playerHeader[64];
        std::snprintf(playerHeader, sizeof(playerHeader), "PLAYERS (%zu)", config.players.size());
        menu_theme::terminalSection(playerHeader);
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

        menu_theme::terminalSection("MATCH SETTINGS");
        if (config.matchConfig) {
            ImGui::Text("Kills to win: %d", config.matchConfig->killsToWin);
            ImGui::Text("Max players: %d", config.matchConfig->maxPlayers);
        } else {
            ImGui::TextDisabled("Waiting for match settings");
        }

        menu_theme::terminalStatusLine("LOBBY COMMANDS", "ARROWS / ENTER");
        if (config.startCountdownActive) {
            ImGui::Text("Entering match countdown in %.1fs", static_cast<double>(config.startCountdownRemaining));
        }

        ImGui::BeginDisabled(config.startCountdownActive);
        if (menu_theme::terminalActionRow(localReady ? "UNREADY" : "READY",
                                          localReady ? "mark yourself not ready" : "mark yourself ready",
                                          ImVec2(0.0f, 32.0f)))
        {
            result.readyChange = !localReady;
        }

        if (config.isHost) {
            ImGui::BeginDisabled(!config.canStartMatch);
            if (menu_theme::terminalActionRow("START MATCH", "launch match for all players", ImVec2(0.0f, 32.0f))) {
                result.startMatchClicked = true;
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();

        if (config.isHost) {
            if (menu_theme::terminalActionRow("BACK TO HOST CONFIG", "edit hosted server", ImVec2(0.0f, 32.0f))) {
                result.returnToHostConfigClicked = true;
            }
        }
        if (menu_theme::terminalActionRow("RETURN TO MAIN MENU", "disconnect from lobby", ImVec2(0.0f, 32.0f), true)) {
            result.returnToMenuClicked = true;
        }
    }
    menu_theme::endPanel();

    return result;
}

} // namespace lobby_ui
