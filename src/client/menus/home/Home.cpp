/// @file Home.cpp
/// @brief Home screen implementation: form handling and frame rendering.

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
    JoinMenuResult result = home_ui::buildJoinMenu(joinMenuState, joinError);
    if (result.connectClicked) {
        joinError.clear();
        SDL_Log("Join button clicked! IP: %s, Port: %d", joinMenuState.serverIp.c_str(), joinMenuState.serverPort);
        if (joinMenuState.serverPort < 1 || joinMenuState.serverPort > 65535) {
            joinError = "Port must be between 1 and 65535";
            SDL_Log("Invalid port number: %d", joinMenuState.serverPort);
        } else {
            pendingJoinRequest = JoinRequest{.serverIp = joinMenuState.serverIp,
                                             .serverPort = static_cast<uint16_t>(joinMenuState.serverPort)};
        }
    }
    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

std::optional<JoinRequest> Home::consumeJoinRequest()
{
    if (!pendingJoinRequest) {
        return std::nullopt;
    }

    std::optional<JoinRequest> result = pendingJoinRequest;
    pendingJoinRequest.reset();
    return result;
}

void Home::setJoinError(const std::string& error)
{
    joinError = error;
}
