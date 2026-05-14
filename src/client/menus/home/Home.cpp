#include "Home.hpp"

#include "ui/HomeUI.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool Home::init(NewRenderer* rendererPtr, SDL_Window* windowPtr)
{
    if (!rendererPtr || !windowPtr)
        return false;

    renderer = rendererPtr;
    window = windowPtr;
    return true;
}

SDL_AppResult Home::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    return SDL_APP_CONTINUE;
}

void Home::quit() {}

SDL_AppResult Home::iterate()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (home_ui::buildJoinMenu()) {
        // TODO: Trigger App transition to Lobby with entered IP/port
        SDL_Log("Join button clicked! (TODO: trigger transition to Lobby with entered IP/port)");
    }
    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}
