#include "Lobby.hpp"

#include "ui/LobbyUI.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool Lobby::init(ClientRenderer* rendererPtr, SDL_Window* windowPtr, Client* clientPtr)
{
    renderer = rendererPtr;
    window = windowPtr;
    client = clientPtr;
    if (!renderer || !window)
        return false;

    // Dummy roster until the lobby is wired to the network.
    players.clear();
    for (int i = 1; i <= 5; ++i)
        players.push_back(LobbyPlayer{ClientId{i}});

    return true;
}

SDL_AppResult Lobby::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult Lobby::iterate()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    lobby_ui::buildPlayerList(players);

    ImGui::Render();

    // Default camera: lobby has no scene, so the renderer just draws sky + ImGui overlay.
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

void Lobby::quit() {}
