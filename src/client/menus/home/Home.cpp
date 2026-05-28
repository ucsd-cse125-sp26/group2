/// @file Home.cpp
/// @brief Home screen implementation: form handling and frame rendering.

#include "Home.hpp"

#include "ui/HomeUI.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <utility>

bool Home::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    discoveryConfig = ctx.networkConfig.discovery;
    startGlobalRefresh(true);

    localDiscoveryClient->start(9998);

    return true;
}

SDL_AppResult Home::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    return SDL_APP_CONTINUE;
}

void Home::quit()
{
    if (browserThread.joinable())
        browserThread.join();

    localDiscoveryClient->stop();
}

SDL_AppResult Home::iterate()
{
    joinRefreshThreadIfFinished();
    if (discoveryConfig.enabled &&
        SDL_GetTicks() - lastBrowserRefreshMs > static_cast<uint64_t>(discoveryConfig.refreshSeconds) * 1000u)
    {
        startGlobalRefresh();
    }

    localDiscoveryClient->poll();
    localServers = localDiscoveryClient->getServers();

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (openPopupMessage) {
        ImGui::OpenPopup("Server Notice");
        openPopupMessage = false;
    }
    if (ImGui::BeginPopupModal("Server Notice", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(popupMessage.c_str());
        ImGui::Spacing();
        if (ImGui::Button("OK")) {
            popupMessage.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    std::vector<net::discovery::ServerInfo> servers;
    std::string globalError;
    {
        std::lock_guard<std::mutex> lock(browserMutex);
        servers = globalServers;
        globalError = browserError;
    }
    JoinMenuResult result = home_ui::buildJoinMenu(joinMenuState,
                                                   joinError,
                                                   localServers,
                                                   servers,
                                                   globalError,
                                                   browserRefreshing.load(std::memory_order_relaxed));
    if (result.refreshClicked) {
        startGlobalRefresh(true);
    }
    if (result.hostClicked) {
        joinError.clear();
        pendingHostRequest = true;
    }

    if (result.connectClicked) {
        joinError.clear();
        SDL_Log("Join button clicked! IP: %s, Port: %d", joinMenuState.serverIp.c_str(), joinMenuState.serverPort);
        if (joinMenuState.serverPort < 1 || joinMenuState.serverPort > 65535) {
            joinError = "Port must be between 1 and 65535";
            SDL_Log("Invalid port number: %d", joinMenuState.serverPort);
        } else {
            pendingJoinRequest = JoinRequest{.serverIp = joinMenuState.serverIp,
                                             .serverPort = static_cast<uint16_t>(joinMenuState.serverPort),
                                             .globalServerId = 0};
        }
    }
    if (result.globalServerIndex >= 0 && result.globalServerIndex < static_cast<int>(servers.size())) {
        const auto& server = servers[static_cast<std::size_t>(result.globalServerIndex)];
        if (server.maxPlayers != 0 && server.currentPlayers >= server.maxPlayers) {
            joinError = "Lobby full";
        } else {
            joinError.clear();
            pendingJoinRequest =
                JoinRequest{.serverIp = server.host, .serverPort = server.gamePort, .globalServerId = server.id};
        }
    } else if (result.localServerIndex >= 0 && result.localServerIndex < static_cast<int>(localServers.size())) {
        const auto& server = localServers[static_cast<std::size_t>(result.localServerIndex)];
        if (server.maxPlayers != 0 && server.currentPlayers >= server.maxPlayers) {
            joinError = "Lobby full";
        } else {
            joinError.clear();
            pendingJoinRequest =
                JoinRequest{.serverIp = server.hostIp, .serverPort = server.gamePort, .globalServerId = 0};
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

bool Home::consumeHostRequest()
{
    if (!pendingHostRequest) {
        return false;
    }

    pendingHostRequest = false;
    return true;
}

void Home::setJoinError(const std::string& error)
{
    joinError = error;
}

void Home::setPopupMessage(const std::string& message)
{
    popupMessage = message;
    openPopupMessage = !popupMessage.empty();
}

void Home::startGlobalRefresh(bool force)
{
    if (!discoveryConfig.enabled)
        return;
    if (browserRefreshing.load(std::memory_order_relaxed))
        return;
    joinRefreshThreadIfFinished();

    const uint64_t now = SDL_GetTicks();
    if (!force && lastBrowserRefreshMs != 0 &&
        now - lastBrowserRefreshMs < static_cast<uint64_t>(discoveryConfig.refreshSeconds) * 1000u)
    {
        return;
    }
    lastBrowserRefreshMs = now;
    browserRefreshing.store(true, std::memory_order_relaxed);

    GlobalDiscoveryConfig cfg = discoveryConfig;
    browserThread = std::thread([this, cfg]() {
        GlobalDiscoveryClient client;
        std::vector<net::discovery::ServerInfo> servers;
        std::string error;
        const bool ok = client.fetchServers(cfg, servers, error);
        {
            std::lock_guard<std::mutex> lock(browserMutex);
            if (ok) {
                globalServers = std::move(servers);
                browserError.clear();
            } else {
                browserError = error.empty() ? "Could not reach directory" : error;
            }
        }
        browserRefreshing.store(false, std::memory_order_relaxed);
    });
}

void Home::joinRefreshThreadIfFinished()
{
    if (browserThread.joinable() && !browserRefreshing.load(std::memory_order_relaxed))
        browserThread.join();
}
