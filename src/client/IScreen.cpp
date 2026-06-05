/// @file IScreen.cpp
/// @brief Shared event and frame helpers used by every menu IScreen.

#include "IScreen.hpp"

#include "menus/MenuTheme.hpp"
#include "menus/settings/SystemMenuOverlay.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <SDL3/SDL_keycode.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

SDL_AppResult IScreen::processCommonImguiEvent(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

bool IScreen::handleSystemMenuEvent(SDL_Event* event, SystemMenuOverlay& menu, UserSettings* settings)
{
    if (settings != nullptr && event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.key == SDLK_ESCAPE)
    {
        if (menu.isOpen()) {
            menu.handleEscape(*settings);
        } else {
            menu.open();
        }
        return true;
    }

    return menu.consumeEvent(*event);
}

void IScreen::beginMenuFrame(NewRenderer* renderer)
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    menu_theme::drawBackground(renderer ? renderer->getDevice() : nullptr);
}

void IScreen::presentMenuFrame(NewRenderer& renderer)
{
    ImGui::Render();
    renderer.drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
}
