#include "Lobby.hpp"

#include "SDL3/SDL_init.h"
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

    client->onLobbyState([this](const std::vector<LobbyPlayer>& snapshot) { players = snapshot; });

    client->onLobbyUpdate([this](const LobbyUpdateEvent& update) {
        switch (update.type) {
        case LobbyUpdateEvent::Type::PlayerJoined:
            SDL_Log("Lobby: player with clientId %u joined", update.id.value);
            players.push_back(LobbyPlayer{update.id});
            break;
        case LobbyUpdateEvent::Type::PlayerLeft:
            SDL_Log("Lobby: player with clientId %u left", update.id.value);
            players.erase(std::remove_if(players.begin(),
                                         players.end(),
                                         [id = update.id](const LobbyPlayer& p) { return p.id == id; }),
                          players.end());
            break;
        case LobbyUpdateEvent::Type::PlayerReadyStatusChanged:
            SDL_Log("Lobby: player with clientId %u changed ready status", update.id.value);
            for (auto& p : players) {
                if (p.id == update.id) {
                    p.ready = !p.ready;
                    break;
                }
            }
            break;
        default:
        }
    });

    // Dummy roster until the lobby is wired to the network.
    // players.clear();
    // for (int i = 1; i <= 5; ++i)
    //     players.push_back(LobbyPlayer{ClientId{i}});

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

    if (!client->poll()) {
        return SDL_APP_SUCCESS;
    }

    lobby_ui::buildPlayerList(players);

    ImGui::Render();

    // Default camera: lobby has no scene, so the renderer just draws sky + ImGui overlay.
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

void Lobby::quit() {}
