/// @file LobbyUI.cpp
/// @brief ImGui implementation of the lobby player-list panel and ready/start controls.
#include "LobbyUI.hpp"

#include "menus/MenuTheme.hpp"

#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>

namespace lobby_ui
{

namespace
{

const char* displayNameFor(const LobbyPlayer& p)
{
    if (p.displayName[0] != '\0')
        return p.displayName.data();
    return nullptr;
}

void drawPlayersTable(const LobbyUIConfig& config)
{
    char playerHeader[64];
    std::snprintf(playerHeader, sizeof(playerHeader), "PLAYERS (%zu)", config.players.size());
    menu_theme::terminalSection(playerHeader);

    if (ImGui::BeginTable("LobbyPlayers",
                          2,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 0.72f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.28f);

        for (const auto& p : config.players) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const bool isLocal = p.id == config.localId;
            char nameBuf[96];
            const char* name = displayNameFor(p);
            if (name == nullptr) {
                std::snprintf(nameBuf,
                              sizeof(nameBuf),
                              "%sPlayer %d%s",
                              isLocal ? "> " : "  ",
                              p.id.value,
                              p.isHost ? " (Host)" : "");
            } else {
                std::snprintf(
                    nameBuf, sizeof(nameBuf), "%s%s%s", isLocal ? "> " : "  ", name, p.isHost ? " (Host)" : "");
            }
            ImGui::TextUnformatted(nameBuf);

            ImGui::TableSetColumnIndex(1);
            const bool effectivelyReady = p.isHost || p.ready;
            if (effectivelyReady)
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "Ready");
            else
                ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.74f, 1.0f), "Not ready");
        }

        ImGui::EndTable();
    }
}

void drawSidebar(const LobbyUIConfig& config, BuildResult& result, bool localReady)
{
    if (config.isHost) {
        menu_theme::terminalSection("HOSTING");
        const float prevScale = ImGui::GetCurrentWindow()->FontWindowScale;
        const float smallScale = prevScale * 0.75f;
        const float reservedHeight = ImGui::GetTextLineHeightWithSpacing() * (smallScale / prevScale);
        const ImVec2 startCursor = ImGui::GetCursorPos();
        if (config.hostAddressesVisible) {
            ImGui::SetWindowFontScale(smallScale);
            if (!config.hostLanIp.empty()) {
                ImGui::Text("Address: %.*s:%u",
                            static_cast<int>(config.hostLanIp.size()),
                            config.hostLanIp.data(),
                            static_cast<unsigned>(config.hostPort));
            } else {
                ImGui::TextDisabled("Address unavailable");
            }
            ImGui::SetWindowFontScale(prevScale);
        } else {
            ImGui::SetWindowFontScale(smallScale);
            ImGui::TextDisabled("Addresses hidden");
            ImGui::SetWindowFontScale(prevScale);
        }
        ImGui::SetCursorPos(ImVec2(startCursor.x, startCursor.y + reservedHeight));
        if (menu_theme::terminalActionRow(
                config.hostAddressesVisible ? "HIDE ADDRESSES" : "SHOW ADDRESSES", nullptr, ImVec2(0.0f, 28.0f)))
        {
            result.hostAddressesVisibilityToggled = true;
        }
        ImGui::Spacing();
    }

    menu_theme::terminalSection("MATCH SETTINGS");
    if (config.matchConfig) {
        ImGui::Text("Kills to win: %d", config.matchConfig->killsToWin);
        ImGui::Text("Max players: %d", config.matchConfig->maxPlayers);
    } else {
        ImGui::TextDisabled("Waiting for match settings");
    }

    menu_theme::terminalStatusLine("LOBBY COMMANDS", "ARROWS / ENTER");
    ImGui::Spacing();
    if (config.startCountdownActive) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f),
                           "MATCH STARTING in %.1fs",
                           static_cast<double>(config.startCountdownRemaining));
    } else {
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
    }

    ImGui::BeginDisabled(config.startCountdownActive);

    if (!config.isHost) {
        if (menu_theme::terminalActionRow(localReady ? "UNREADY" : "READY", nullptr, ImVec2(0.0f, 32.0f))) {
            result.readyChange = !localReady;
        }
    }

    if (config.isHost) {
        ImGui::BeginDisabled(!config.canStartMatch);
        if (menu_theme::terminalActionRow("START MATCH", nullptr, ImVec2(0.0f, 32.0f))) {
            result.startMatchClicked = true;
        }
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();

    if (config.isHost) {
        // Disable while a match start is in flight; returning to host config
        // mid-countdown lets "GO TO LOBBY" re-trigger the start.
        ImGui::BeginDisabled(config.startCountdownActive);
        if (menu_theme::terminalActionRow("BACK TO HOST CONFIG", nullptr, ImVec2(0.0f, 32.0f))) {
            result.returnToHostConfigClicked = true;
        }
        ImGui::EndDisabled();
    }
    if (config.isHost) {
        ImGui::BeginDisabled(!config.startCountdownActive);
        if (menu_theme::terminalActionRow("CANCEL COUNTDOWN", nullptr, ImVec2(0.0f, 32.0f), true)) {
            result.cancelStartMatchClicked = true;
        }
        ImGui::EndDisabled();
    }
    if (menu_theme::terminalActionRow("RETURN TO MAIN MENU", nullptr, ImVec2(0.0f, 32.0f), true)) {
        result.returnToMenuClicked = true;
    }
}

} // namespace

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

    if (menu_theme::beginPanel("Lobby",
                               menu_theme::k_frontendPanelBaseWidth,
                               menu_theme::k_frontendPanelBaseHeight,
                               true,
                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        menu_theme::terminalStatusLine(config.startCountdownActive ? "MATCH COUNTDOWN ACTIVE" : "WAITING FOR READY",
                                       config.isHost ? "HOST" : "CLIENT");
        if (!config.serverName.empty()) {
            ImGui::Text("Server: %.*s", static_cast<int>(config.serverName.size()), config.serverName.data());
        }

        if (ImGui::BeginTable("LobbyLayout", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Sidebar", ImGuiTableColumnFlags_WidthStretch, 0.38f);
            ImGui::TableSetupColumn("Roster", ImGuiTableColumnFlags_WidthStretch, 0.62f);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            drawSidebar(config, result, localReady);

            ImGui::TableSetColumnIndex(1);
            drawPlayersTable(config);

            ImGui::EndTable();
        }
    }
    menu_theme::endPanel();

    return result;
}

} // namespace lobby_ui
