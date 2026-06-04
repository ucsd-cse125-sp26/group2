/// @file MainMenu.cpp
/// @brief MainMenu screen implementation: form handling and frame rendering.

#include "MainMenu.hpp"

#include "menus/MenuTheme.hpp"
#include "ui/MainMenuUI.hpp"
#include "util/InputCapture.hpp"
#include "util/LocalAddress.hpp"

#include <SDL3/SDL_keycode.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <utility>

namespace
{

const DiscoveryClient::DiscoveredServer*
findLocalMirror(const net::discovery::ServerInfo& globalServer,
                const std::vector<DiscoveryClient::DiscoveredServer>& localServers)
{
    for (const auto& localServer : localServers) {
        if (localServer.globalServerId != 0 && localServer.globalServerId == globalServer.id)
            return &localServer;
    }
    // Fallback while the local discovery packet is still waiting for the directory id.
    for (const auto& localServer : localServers) {
        if (localServer.globalServerId == 0 && localServer.gamePort == globalServer.gamePort &&
            localServer.serverName == globalServer.name)
        {
            return &localServer;
        }
    }
    return nullptr;
}

std::string localRouteHost(const DiscoveryClient::DiscoveredServer& localServer)
{
    const std::string lanIp = local_address::firstLanIPv4();
    if (localServer.hostIp == "127.0.0.1" || (!lanIp.empty() && localServer.hostIp == lanIp))
        return "127.0.0.1";
    return localServer.hostIp;
}

} // namespace

bool MainMenu::init(AppContext& ctx)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    settings = &ctx.userSettings;
    settingsPath = ctx.userSettingsPath;

    // Defensive: menus always run with a free desktop cursor.
    input_capture::releaseGameplayInputCapture(window);

    discoveryConfig = ctx.networkConfig.discovery;
    startGlobalRefresh(true);

    localDiscoveryClient->start(discoveryConfig.lanBroadcastPort);

    return true;
}

SDL_AppResult MainMenu::event(SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    if (settings != nullptr && event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.key == SDLK_ESCAPE)
    {
        if (systemMenu_.isOpen()) {
            systemMenu_.handleEscape(*settings);
        } else {
            systemMenu_.open();
        }
        return SDL_APP_CONTINUE;
    }

    if (systemMenu_.consumeEvent(*event))
        return SDL_APP_CONTINUE;

    return SDL_APP_CONTINUE;
}

void MainMenu::quit()
{
    if (browserThread.joinable())
        browserThread.join();

    localDiscoveryClient->stop();
}

SDL_AppResult MainMenu::iterate()
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
    menu_theme::drawBackground(renderer ? renderer->getDevice() : nullptr);
    if (openPopupMessage) {
        ImGui::OpenPopup("Server Notice");
        openPopupMessage = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Server Notice", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
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
    JoinMenuResult result = main_menu_ui::buildJoinMenu(joinMenuState,
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
    if (result.returnToTitleScreenClicked) {
        joinError.clear();
        pendingReturnToTitleScreenRequest = true;
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
                                             .globalServerId = 0,
                                             .serverName = joinMenuState.serverIp};
        }
    }
    if (result.globalServerIndex >= 0 && result.globalServerIndex < static_cast<int>(servers.size())) {
        const auto& server = servers[static_cast<std::size_t>(result.globalServerIndex)];
        if (server.maxPlayers != 0 && server.currentPlayers >= server.maxPlayers) {
            joinError = "Lobby full";
        } else {
            joinError.clear();
            if (const auto* localMirror = findLocalMirror(server, localServers)) {
                const std::string routeHost = localRouteHost(*localMirror);
                SDL_Log("MainMenu: global server '%s' is visible locally at %s:%u; using %s:%u",
                        server.name.c_str(),
                        localMirror->hostIp.c_str(),
                        localMirror->gamePort,
                        routeHost.c_str(),
                        localMirror->gamePort);
                pendingJoinRequest = JoinRequest{.serverIp = routeHost,
                                                 .serverPort = localMirror->gamePort,
                                                 .globalServerId = 0,
                                                 .serverName = server.name};
            } else {
                const std::string serverIp = server.udpHost.empty() ? server.host : server.udpHost;
                const uint16_t serverPort = server.udpPort != 0 ? server.udpPort : server.gamePort;
                pendingJoinRequest = JoinRequest{.serverIp = serverIp,
                                                 .serverPort = serverPort,
                                                 .globalServerId = server.id,
                                                 .serverName = server.name};
            }
        }
    } else if (result.localServerIndex >= 0 && result.localServerIndex < static_cast<int>(localServers.size())) {
        const auto& server = localServers[static_cast<std::size_t>(result.localServerIndex)];
        if (server.maxPlayers != 0 && server.currentPlayers >= server.maxPlayers) {
            joinError = "Lobby full";
        } else {
            joinError.clear();
            pendingJoinRequest = JoinRequest{.serverIp = server.hostIp,
                                             .serverPort = server.gamePort,
                                             .globalServerId = 0,
                                             .serverName = server.serverName};
        }
    }

    if (settings != nullptr) {
        const SystemMenuOverlayResult menuResult = systemMenu_.render(*settings, settingsPath);
        if (menuResult.exitToDesktop) {
            pendingExitRequest = true;
        }
    }

    ImGui::Render();
    renderer->drawFrame(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f);
    return SDL_APP_CONTINUE;
}

std::optional<JoinRequest> MainMenu::consumeJoinRequest()
{
    if (!pendingJoinRequest) {
        return std::nullopt;
    }

    std::optional<JoinRequest> result = pendingJoinRequest;
    pendingJoinRequest.reset();
    return result;
}

bool MainMenu::consumeHostRequest()
{
    if (!pendingHostRequest) {
        return false;
    }

    pendingHostRequest = false;
    return true;
}

bool MainMenu::consumeReturnToTitleScreenRequest()
{
    if (!pendingReturnToTitleScreenRequest) {
        return false;
    }

    pendingReturnToTitleScreenRequest = false;
    return true;
}

bool MainMenu::consumeExitRequest()
{
    if (!pendingExitRequest)
        return false;

    pendingExitRequest = false;
    return true;
}

void MainMenu::setJoinError(const std::string& error)
{
    joinError = error;
}

void MainMenu::setJoinInProgress(bool joining, const std::string& label)
{
    joinMenuState.joining = joining;
    joinMenuState.joiningLabel = joining ? label : std::string{};
}

void MainMenu::setPopupMessage(const std::string& message)
{
    popupMessage = message;
    openPopupMessage = !popupMessage.empty();
}

void MainMenu::startGlobalRefresh(bool force)
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

void MainMenu::joinRefreshThreadIfFinished()
{
    if (browserThread.joinable() && !browserRefreshing.load(std::memory_order_relaxed))
        browserThread.join();
}
