/// @file LoadingScreen.cpp
/// @brief Terminal-style loading screen implementation.

#include "LoadingScreen.hpp"

#include "menus/MenuTheme.hpp"

#include <SDL3/SDL_events.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
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
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult LoadingScreen::iterate()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    menu_theme::drawBackground(renderer ? renderer->getDevice() : nullptr);

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

    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    renderedFrame = true;
    return SDL_APP_CONTINUE;
}

void LoadingScreen::quit() {}

bool LoadingScreen::readyToStartGame() const
{
    return renderedFrame;
}
