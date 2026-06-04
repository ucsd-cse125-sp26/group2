/// @file LoadingScreen.cpp
/// @brief Terminal-style loading screen implementation.

#include "LoadingScreen.hpp"

#include "menus/MenuTheme.hpp"

#include <SDL3/SDL_events.h>

#include <imgui.h>

bool LoadingScreen::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    serverName = std::string(ctx.currentServerName);
    if (serverName.empty() && ctx.hostedServer.hasSession())
        serverName = ctx.hostConfigState.serverName;
    renderedFrame = false;
    return renderer != nullptr && window != nullptr;
}

SDL_AppResult LoadingScreen::event(SDL_Event* event)
{
    return processCommonImguiEvent(event);
}

SDL_AppResult LoadingScreen::iterate()
{
    beginMenuFrame(renderer);

    if (menu_theme::beginPanel(
            "Loading", menu_theme::k_frontendPanelBaseWidth, menu_theme::k_frontendPanelBaseHeight, true))
    {
        menu_theme::terminalStatusLine("INITIALIZING MATCH", "PLEASE STAND BY");

        if (!serverName.empty()) {
            ImGui::Text("Server: %s", serverName.c_str());
            ImGui::Spacing();
        }

        menu_theme::terminalSection("MATCH BOOT SEQUENCE");
        ImGui::TextUnformatted("Loading map collision");
        ImGui::TextUnformatted("Uploading render assets");
        ImGui::TextUnformatted("Initializing audio, HUD, and animation");
        ImGui::TextUnformatted("Synchronizing with warmup readiness");

        ImGui::Spacing();
        menu_theme::terminalStatusLine("GAMEPLAY READY SIGNAL WILL BE SENT AFTER INITIALIZATION");
    }
    menu_theme::endPanel();

    presentMenuFrame(*renderer);
    renderedFrame = true;
    return SDL_APP_CONTINUE;
}

void LoadingScreen::quit() {}

bool LoadingScreen::readyToStartGame() const
{
    return renderedFrame;
}
