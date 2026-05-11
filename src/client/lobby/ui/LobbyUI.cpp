#include "LobbyUI.hpp"

#include <imgui.h>

namespace lobby_ui
{

void buildPlayerList(const std::vector<LobbyPlayer>& players, ClientId localId)
{
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
        }
    }
    ImGui::End();
}

} // namespace lobby_ui
