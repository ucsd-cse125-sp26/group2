/// @file PostMatchScoreboard.cpp
/// @brief Dedicated post-match scoreboard screen implementation.

#include "PostMatchScoreboard.hpp"

#include "menus/MenuTheme.hpp"
#include "util/InputCapture.hpp"

#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool PostMatchScoreboard::init(AppContext& ctx, PostMatchResult result)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    client = &ctx.client;
    result_ = std::move(result);

    input_capture::releaseGameplayInputCapture(window);

    std::sort(result_.rows.begin(), result_.rows.end(), [](const PostMatchScoreRow& a, const PostMatchScoreRow& b) {
        if (a.kills != b.kills)
            return a.kills > b.kills;
        if (a.deaths != b.deaths)
            return a.deaths < b.deaths;
        return a.name < b.name;
    });

    return renderer != nullptr && window != nullptr && client != nullptr;
}

SDL_AppResult PostMatchScoreboard::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult PostMatchScoreboard::iterate()
{
    if (!client->poll()) {
        SDL_Log("PostMatchScoreboard: lost connection to server; returning to main menu");
        returnToMenu_ = true;
        serverShutdownNotice_ = true;
        return SDL_APP_CONTINUE;
    }

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    menu_theme::drawBackground(renderer ? renderer->getDevice() : nullptr);

    if (menu_theme::beginPanel("Match Complete", 700.0f, 520.0f, false)) {
        const ImGuiIO& io = ImGui::GetIO();
        const float scale = menu_theme::scaleFor(io.DisplaySize);
        const char* resultText = result_.won ? "VICTORY" : "DEFEAT";
        const ImVec4 resultColor = result_.won ? ImVec4(0.35f, 0.95f, 0.50f, 1.0f) : ImVec4(0.95f, 0.22f, 0.20f, 1.0f);

        ImGui::SetWindowFontScale(scale * 2.3f);
        const ImVec2 titleSize = ImGui::CalcTextSize(resultText);
        const float titleX = (ImGui::GetContentRegionAvail().x - titleSize.x) * 0.5f;
        if (titleX > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + titleX);
        ImGui::PushStyleColor(ImGuiCol_Text, resultColor);
        ImGui::TextUnformatted(resultText);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(scale);

        ImGui::Spacing();
        menu_theme::heading("Final Scoreboard");

        const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
        if (ImGui::BeginTable("postmatch-scoreboard", 3, flags)) {
            ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 2.5f);
            ImGui::TableSetupColumn("K", ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthStretch, 0.5f);
            ImGui::TableHeadersRow();

            for (const PostMatchScoreRow& row : result_.rows) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (row.isLocal)
                    ImGui::Text("%s (You)", row.name.c_str());
                else
                    ImGui::TextUnformatted(row.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d", row.kills);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", row.deaths);
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (menu_theme::accentButton("Return to Lobby", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
            returnToLobby_ = true;
        }
    }
    menu_theme::endPanel();

    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

void PostMatchScoreboard::quit() {}

bool PostMatchScoreboard::consumeReturnToLobby()
{
    if (!returnToLobby_)
        return false;
    returnToLobby_ = false;
    return true;
}

bool PostMatchScoreboard::consumeReturnToMenu()
{
    if (!returnToMenu_)
        return false;
    returnToMenu_ = false;
    return true;
}

bool PostMatchScoreboard::consumeServerShutdownNotice()
{
    if (!serverShutdownNotice_)
        return false;
    serverShutdownNotice_ = false;
    return true;
}
