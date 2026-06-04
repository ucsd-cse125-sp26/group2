/// @file SettingsScreen.cpp
/// @brief Dedicated front-end settings screen lifecycle and rendering.

#include "SettingsScreen.hpp"

#include "menus/MenuTheme.hpp"
#include "util/InputCapture.hpp"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool SettingsScreen::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    settings = &ctx.userSettings;
    settingsPath = ctx.userSettingsPath;

    input_capture::releaseGameplayInputCapture(window);
    editor.open(*settings);

    return renderer != nullptr && window != nullptr && settings != nullptr;
}

SDL_AppResult SettingsScreen::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    if (settings != nullptr && event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.key == SDLK_ESCAPE)
    {
        if (editor.handleEscape(*settings)) {
            pendingBack = true;
        }
        return SDL_APP_CONTINUE;
    }

    editor.consumeEvent(*event);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SettingsScreen::iterate()
{
    if (!renderer || !settings)
        return SDL_APP_FAILURE;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    menu_theme::drawBackground(renderer->getDevice());

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float uiScale = std::clamp(menu_theme::scaleFor(display), 0.5f, 1.0f);
    if (menu_theme::beginPanel("Settings",
                               menu_theme::k_frontendPanelBaseWidth,
                               menu_theme::k_frontendPanelBaseHeight,
                               false,
                               ImGuiWindowFlags_NoResize))
    {
        const SettingsEditorResult result = editor.render(*settings, settingsPath, uiScale);
        if (result.closeRequested) {
            pendingBack = true;
        }
    }
    menu_theme::endPanel();

    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

void SettingsScreen::quit()
{
    editor.close();
}

bool SettingsScreen::consumeBackRequest()
{
    if (!pendingBack)
        return false;

    pendingBack = false;
    return true;
}
