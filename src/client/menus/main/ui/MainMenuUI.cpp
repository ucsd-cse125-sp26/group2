/// @file MainMenuUI.cpp
/// @brief ImGui implementation of the tabbed server browser widget.

#include "MainMenuUI.hpp"

#include "menus/MenuTheme.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace main_menu_ui
{
namespace
{
void drawDirectConnect(JoinMenuState& state,
                       std::string_view errorMessage,
                       JoinMenuResult& result,
                       bool directConnectDisabled)
{
    menu_theme::terminalSection("DIRECT CONNECT");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##ServerAddress", "Input Server Address", &state.serverAddress);

    const ImVec2 actionSize(0.0f, 32.0f);
    ImGui::BeginDisabled(state.joining || directConnectDisabled);
    if (menu_theme::terminalActionRow(
            state.joining ? "CONNECTING..." : "JOIN", "connect to entered address", actionSize))
    {
        result.connectClicked = true;
    }
    ImGui::EndDisabled();
    if (directConnectDisabled) {
        ImGui::TextDisabled("Already connected to a server session");
    }
    if (state.joining) {
        ImGui::Spacing();
        ImGui::TextColored(menu_theme::settings().accent, "CONNECTING TO %s", state.joiningLabel.c_str());
    }
    if (!errorMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(
            menu_theme::settings().dangerActive, "%.*s", static_cast<int>(errorMessage.size()), errorMessage.data());
    }
}

void drawLocalServers(JoinMenuState& state,
                      const std::vector<DiscoveryClient::DiscoveredServer>& localServers,
                      JoinMenuResult& result,
                      bool directConnectDisabled)
{
    menu_theme::terminalSection("LOCAL SERVERS");
    ImGui::BeginDisabled(state.joining);
    if (menu_theme::terminalActionRow("REFRESH", "scan LAN", ImVec2(0.0f, 30.0f))) {
        result.localRefreshClicked = true;
    }
    ImGui::EndDisabled();

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
            ImGui::BeginDisabled(lobbyFull || state.joining || directConnectDisabled);
            if (menu_theme::terminalActionRow("CONNECT", nullptr, ImVec2(0.0f, 0.0f))) {
                result.localServerIndex = i;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void drawGlobalServers(JoinMenuState& state,
                       const std::vector<net::discovery::ServerInfo>& globalServers,
                       std::string_view browserError,
                       bool browserRefreshing,
                       JoinMenuResult& result,
                       bool directConnectDisabled)
{
    menu_theme::terminalSection("GLOBAL SERVERS");
    ImGui::BeginDisabled(state.joining);
    if (menu_theme::terminalActionRow(
            browserRefreshing ? "REFRESHING..." : "REFRESH", "query directory", ImVec2(0.0f, 30.0f)))
    {
        result.refreshClicked = true;
    }
    ImGui::EndDisabled();
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
            ImGui::BeginDisabled(lobbyFull || state.joining || directConnectDisabled);
            if (menu_theme::terminalActionRow("CONNECT", nullptr, ImVec2(0.0f, 0.0f))) {
                result.globalServerIndex = i;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}
} // namespace

JoinMenuResult buildJoinMenu(JoinMenuState& state,
                             std::string_view errorMessage,
                             const std::vector<DiscoveryClient::DiscoveredServer>& localServers,
                             const std::vector<net::discovery::ServerInfo>& globalServers,
                             std::string_view browserError,
                             bool browserRefreshing,
                             bool directConnectDisabled,
                             const HostConfigUIInputs& hostInputs)
{
    JoinMenuResult result{};
    const ServerBrowserTab previousTab = state.activeTab;

    if (menu_theme::beginPanel("Server Browser",
                               menu_theme::k_frontendPanelBaseWidth,
                               menu_theme::k_frontendPanelBaseHeight,
                               true,
                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        menu_theme::terminalStatusLine("NETSCAN READY", "SELECT TAB OR TYPE ADDRESS");
        if (ImGui::BeginTabBar("ServerBrowserTabs")) {
            const bool applyingInitialTabSelection = state.applyInitialTabSelection;
            ImGuiTabItemFlags localFlags = ImGuiTabItemFlags_None;
            ImGuiTabItemFlags globalFlags = ImGuiTabItemFlags_None;
            ImGuiTabItemFlags directFlags = ImGuiTabItemFlags_None;
            ImGuiTabItemFlags hostFlags = ImGuiTabItemFlags_None;
            if (state.applyInitialTabSelection && state.activeTab == ServerBrowserTab::LocalListing)
                localFlags = ImGuiTabItemFlags_SetSelected;
            if (state.applyInitialTabSelection && state.activeTab == ServerBrowserTab::GlobalListing)
                globalFlags = ImGuiTabItemFlags_SetSelected;
            if (state.applyInitialTabSelection && state.activeTab == ServerBrowserTab::DirectConnect)
                directFlags = ImGuiTabItemFlags_SetSelected;
            if (state.applyInitialTabSelection && state.activeTab == ServerBrowserTab::HostConfig)
                hostFlags = ImGuiTabItemFlags_SetSelected;
            state.applyInitialTabSelection = false;

            // Reserve room below the tab for the BACK action row + its status line,
            // so the list tabs scroll internally and BACK stays pinned at the panel bottom.
            const float spacingY = ImGui::GetStyle().ItemSpacing.y;
            const float backRowHeight = std::max(32.0f, ImGui::GetFontSize() + 16.0f);
            const float footerReserve = backRowHeight + ImGui::GetTextLineHeightWithSpacing() + spacingY * 3.0f;

            if (ImGui::BeginTabItem("Local", nullptr, localFlags)) {
                state.activeTab = ServerBrowserTab::LocalListing;
                if (ImGui::BeginChild("##LocalScroll",
                                      ImVec2(0.0f, -footerReserve),
                                      ImGuiChildFlags_None,
                                      ImGuiWindowFlags_NavFlattened))
                {
                    drawLocalServers(state, localServers, result, directConnectDisabled);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Global", nullptr, globalFlags)) {
                state.activeTab = ServerBrowserTab::GlobalListing;
                if (ImGui::BeginChild("##GlobalScroll",
                                      ImVec2(0.0f, -footerReserve),
                                      ImGuiChildFlags_None,
                                      ImGuiWindowFlags_NavFlattened))
                {
                    drawGlobalServers(
                        state, globalServers, browserError, browserRefreshing, result, directConnectDisabled);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Direct", nullptr, directFlags)) {
                state.activeTab = ServerBrowserTab::DirectConnect;
                if (ImGui::BeginChild("##DirectScroll",
                                      ImVec2(0.0f, -footerReserve),
                                      ImGuiChildFlags_None,
                                      ImGuiWindowFlags_NavFlattened))
                {
                    drawDirectConnect(state, errorMessage, result, directConnectDisabled);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Host", nullptr, hostFlags)) {
                state.activeTab = ServerBrowserTab::HostConfig;
                if (ImGui::BeginChild("##HostScroll",
                                      ImVec2(0.0f, -footerReserve),
                                      ImGuiChildFlags_None,
                                      ImGuiWindowFlags_NavFlattened))
                {
                    result.hostConfig = host_config_ui::buildHostConfigContents(hostInputs, false);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            if (!applyingInitialTabSelection && state.activeTab != previousTab)
                menu_theme::playUiSound(UiSoundAction::Toggle);
        }

        menu_theme::terminalStatusLine("BROWSER ONLINE", "ESC: SYSTEM MENU");
        ImGui::BeginDisabled(state.joining);
        if (menu_theme::terminalActionRow("BACK", "return to title screen", ImVec2(0.0f, 32.0f))) {
            result.returnToTitleScreenClicked = true;
        }
        ImGui::EndDisabled();
    }
    menu_theme::endPanel();

    return result;
}
} // namespace main_menu_ui
