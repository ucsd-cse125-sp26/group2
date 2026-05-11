#include "LobbyUI.hpp"

#include <imgui.h>

namespace lobby_ui
{

std::optional<bool> buildPlayerList(const std::vector<LobbyPlayer>& players, ClientId localId)
{
    bool localReady = false;
    for (const auto& p : players) {
        if (p.id == localId) {
            localReady = p.ready;
            break;
        }
    }

    std::optional<bool> readyChange;

    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(320.0f, 240.0f), ImGuiCond_Once);
    if (ImGui::Begin("Lobby")) {
        ImGui::Text("Players (%zu)", players.size());
        ImGui::Separator();
        for (const auto& p : players) {
            const bool isLocal = p.id == localId;
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
            readyChange = !localReady;
    }
    ImGui::End();

    return readyChange;
}

} // namespace lobby_ui
