#include "LobbyUI.hpp"

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

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f), ImGuiCond_Once);
    if (ImGui::Begin("Lobby")) {
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
            ImGui::TextUnformatted(p.ready ? "Ready" : "Not ready");
        }

        ImGui::Separator();
        if (ImGui::Button(localReady ? "Unready" : "Ready"))
            result.readyChange = !localReady;

        if (config.isHost) {
            ImGui::SameLine();
            ImGui::BeginDisabled(!config.canStartMatch);
            if (ImGui::Button("Start Match"))
                result.startMatchClicked = true;
            ImGui::EndDisabled();
        }
    }
    ImGui::End();

    return result;
}

} // namespace lobby_ui
