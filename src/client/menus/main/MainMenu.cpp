/// @file MainMenu.cpp
/// @brief Landing menu screen lifecycle and frame rendering.

#include "MainMenu.hpp"

#include "menus/MenuTheme.hpp"
#include "ui/MainMenuUI.hpp"
#include "util/InputCapture.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool MainMenu::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;

    input_capture::releaseGameplayInputCapture(window);

    return renderer != nullptr && window != nullptr;
}

SDL_AppResult MainMenu::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    return SDL_APP_CONTINUE;
}

SDL_AppResult MainMenu::iterate()
{
    if (!renderer)
        return SDL_APP_FAILURE;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    menu_theme::drawBackground(renderer->getDevice());

    const MainMenuResult result = main_menu_ui::buildMainMenu();
    if (result.playClicked) {
        pendingPlay = true;
    }
    if (result.hostClicked) {
        pendingHost = true;
    }
    if (result.exitClicked) {
        pendingExit = true;
    }

    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

void MainMenu::quit() {}

bool MainMenu::consumePlayRequest()
{
    if (!pendingPlay)
        return false;

    pendingPlay = false;
    return true;
}

bool MainMenu::consumeHostRequest()
{
    if (!pendingHost)
        return false;

    pendingHost = false;
    return true;
}

bool MainMenu::consumeExitRequest()
{
    if (!pendingExit)
        return false;

    pendingExit = false;
    return true;
}
