/// @file HostConfig.cpp
/// @brief Host-configuration screen lifecycle and App transition requests.

#include "HostConfig.hpp"

#include "ui/HostConfigUI.hpp"

#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>

bool HostConfig::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    client = &ctx.client;
    hostedServer = &ctx.hostedServer;
    draft = &ctx.hostConfigState;
    if (!hostedServer->isRunning() && !draft->useSpecificPort) {
        draft->port = ctx.networkConfig.serverNetwork.port;
    }
    return true;
}

SDL_AppResult HostConfig::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    return SDL_APP_CONTINUE;
}

void HostConfig::quit() {}

SDL_AppResult HostConfig::iterate()
{
    if (!renderer || !hostedServer || !draft)
        return SDL_APP_FAILURE;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const bool serverRunning = hostedServer->isRunning();
    if (serverRunning && client && !client->poll()) {
        lastError = "Lost connection to hosted server";
    }

    HostConfigUIInputs inputs{
        .draft = *draft,
        .serverRunning = serverRunning,
        .boundPort = hostedServer->port(),
        .errorMessage = lastError,
    };

    const HostConfigResult result = host_config_ui::buildHostConfigMenu(inputs);
    if (result.launchClicked) {
        lastError.clear();
        pendingLaunch = true;
    }
    if (result.shutdownClicked) {
        lastError.clear();
        pendingShutdown = true;
    }
    if (result.goToLobbyClicked) {
        pendingGoToLobby = true;
    }
    if (result.backToHomeClicked) {
        pendingBackToHome = true;
    }

    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

bool HostConfig::consumeLaunchRequest()
{
    if (!pendingLaunch)
        return false;

    pendingLaunch = false;
    return true;
}

bool HostConfig::consumeShutdownRequest()
{
    if (!pendingShutdown)
        return false;

    pendingShutdown = false;
    return true;
}

bool HostConfig::consumeGoToLobbyRequest()
{
    if (!pendingGoToLobby)
        return false;

    pendingGoToLobby = false;
    return true;
}

bool HostConfig::consumeBackToHomeRequest()
{
    if (!pendingBackToHome)
        return false;

    pendingBackToHome = false;
    return true;
}

HostConfigState HostConfig::draftConfig() const
{
    if (!draft)
        return HostConfigState{
            .port = 9999,
            .useSpecificPort = false,
            .useLegacyTcp = false,
            .persistAfterClientExit = false,
        };

    HostConfigState result = *draft;
    result.port = std::clamp(result.port, 0, 65535);
    return result;
}

void HostConfig::setLaunchError(const std::string& error)
{
    lastError = error;
}
