/// @file MainMenu.cpp
/// @brief MainMenu screen implementation: form handling and frame rendering.

#include "MainMenu.hpp"

#include "menus/MenuTheme.hpp"
#include "network/ServerName.hpp"
#include "ui/MainMenuUI.hpp"
#include "util/InputCapture.hpp"
#include "util/LocalAddress.hpp"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <cctype>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <optional>
#include <string_view>
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

std::string_view trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

struct ParsedAddress
{
    std::string host;
    uint16_t port = 0;
};

std::optional<uint16_t> parsePort(std::string_view value)
{
    value = trim(value);
    if (value.empty())
        return std::nullopt;

    int port = 0;
    for (const char c : value) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return std::nullopt;
        port = port * 10 + (c - '0');
        if (port > 65535)
            return std::nullopt;
    }
    if (port < 1)
        return std::nullopt;
    return static_cast<uint16_t>(port);
}

std::optional<ParsedAddress> parseServerAddress(std::string_view rawAddress, uint16_t defaultPort)
{
    std::string_view address = trim(rawAddress);
    if (address.empty())
        return std::nullopt;

    if (const std::size_t scheme = address.find("://"); scheme != std::string_view::npos) {
        address.remove_prefix(scheme + 3);
    }
    if (const std::size_t query = address.find_first_of("?#"); query != std::string_view::npos) {
        address = address.substr(0, query);
    }
    if (!address.empty() && address.front() == '[') {
        const std::size_t close = address.find(']');
        if (close == std::string_view::npos)
            return std::nullopt;

        const std::string host(address.substr(1, close - 1));
        uint16_t port = defaultPort;
        std::string_view rest = address.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':')
                return std::nullopt;
            rest.remove_prefix(1);
            if (const std::size_t slash = rest.find('/'); slash != std::string_view::npos) {
                rest = rest.substr(0, slash);
            }
            const auto parsedPort = parsePort(rest);
            if (!parsedPort)
                return std::nullopt;
            port = *parsedPort;
        }
        if (host.empty() || port == 0)
            return std::nullopt;
        return ParsedAddress{.host = host, .port = port};
    }

    if (const std::size_t slash = address.find('/'); slash != std::string_view::npos) {
        address = address.substr(0, slash);
    }
    address = trim(address);
    if (address.empty())
        return std::nullopt;

    std::string host(address);
    uint16_t port = defaultPort;
    const std::size_t colon = address.rfind(':');
    if (colon != std::string_view::npos && address.find(':') == colon) {
        host = std::string(trim(address.substr(0, colon)));
        const auto parsedPort = parsePort(address.substr(colon + 1));
        if (!parsedPort)
            return std::nullopt;
        port = *parsedPort;
    }
    if (host.empty() || port == 0)
        return std::nullopt;
    return ParsedAddress{.host = host, .port = port};
}

} // namespace

bool MainMenu::init(AppContext& ctx, ServerBrowserTab initialTab)
{
    renderer = &ctx.renderer;
    window = &ctx.window;
    client = &ctx.client;
    hostedServer = &ctx.hostedServer;
    draft = &ctx.hostConfigState;
    networkConfig = &ctx.networkConfig;
    settings = &ctx.userSettings;
    settingsPath = ctx.userSettingsPath;
    joinMenuState.activeTab = initialTab;
    joinMenuState.applyInitialTabSelection = true;

    // Defensive: menus always run with a free desktop cursor.
    input_capture::releaseGameplayInputCapture(window);

    discoveryConfig = ctx.networkConfig.discovery;
    if (!hostedServer->isRunning() && !draft->useSpecificPort) {
        draft->port = ctx.networkConfig.serverNetwork.port;
    }
    if (const auto latestMatchConfig = client->getLatestMatchConfig()) {
        lastSyncedMatchConfig = latestMatchConfig;
    } else {
        const HostConfigState initialDraft = draftConfig();
        lastSyncedMatchConfig =
            MatchConfig{.killsToWin = initialDraft.killsToWin, .maxPlayers = initialDraft.maxPlayers};
    }
    const HostConfigState initialDraft = draftConfig();
    lastSyncedDiscoverySettings =
        DiscoverySettings{.advertiseGlobal = initialDraft.advertiseGlobal, .advertiseLan = initialDraft.advertiseLan};
    client->onMatchConfig([this](const MatchConfig& config) { lastSyncedMatchConfig = config; });

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

    if (client) {
        client->onMatchConfig({});
    }
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
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
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
    const bool ownsLocalProcess = hostedServer && hostedServer->isRunning();
    const bool serverRunning = ownsLocalProcess || (client && client->isConnected());
    if (serverRunning && client && !client->poll()) {
        lastHostError = "Lost connection to hosted server";
    }
    const bool canManageServer = canManageCurrentServer() || ownsLocalProcess;
    const bool hasUnsavedChanges = serverRunning && hasUnsavedServerChanges();
    HostConfigUIInputs hostInputs{
        .draft = *draft,
        .serverRunning = serverRunning,
        .canManageServer = canManageServer,
        .ownsLocalProcess = ownsLocalProcess,
        .hasUnsavedServerChanges = hasUnsavedChanges,
        .boundPort = hostedServer ? hostedServer->port() : static_cast<uint16_t>(0),
        .errorMessage = lastHostError,
    };
    JoinMenuResult result = main_menu_ui::buildJoinMenu(joinMenuState,
                                                        joinError,
                                                        localServers,
                                                        servers,
                                                        globalError,
                                                        browserRefreshing.load(std::memory_order_relaxed),
                                                        serverRunning,
                                                        hostInputs);
    if (result.refreshClicked) {
        startGlobalRefresh(true);
    }
    if (result.localRefreshClicked) {
        localDiscoveryClient->refresh(true);
        localServers = localDiscoveryClient->getServers();
    }
    if (result.returnToTitleScreenClicked) {
        joinError.clear();
        pendingReturnToTitleScreenRequest = true;
    }

    if (result.connectClicked) {
        joinError.clear();
        const uint16_t defaultPort = networkConfig ? networkConfig->clientNetwork.port : 9999;
        const auto parsedAddress = parseServerAddress(joinMenuState.serverAddress, defaultPort);
        if (!parsedAddress) {
            joinError = "Enter a valid server address";
            SDL_Log("Invalid server address: %s", joinMenuState.serverAddress.c_str());
        } else {
            SDL_Log("Join button clicked! Address: %s:%u",
                    parsedAddress->host.c_str(),
                    static_cast<unsigned>(parsedAddress->port));
            pendingJoinRequest = JoinRequest{.serverIp = parsedAddress->host,
                                             .serverPort = parsedAddress->port,
                                             .globalServerId = 0,
                                             .serverName = joinMenuState.serverAddress};
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

    const HostConfigResult& hostResult = result.hostConfig;
    if (hostResult.launchClicked) {
        lastHostError.clear();
        pendingLaunch = true;
    }
    if (hostResult.updateClicked) {
        updateServerSettings();
    }
    if (hostResult.shutdownClicked) {
        requestShutdownConfirm();
    }
    if (hostResult.goToLobbyClicked) {
        if (hasUnsavedChanges) {
            requestDiscardMatchChangesConfirm();
        } else {
            pendingGoToLobby = true;
        }
    }
    if (hostResult.backToMainMenuClicked) {
        pendingReturnToTitleScreenRequest = true;
    }

    const ConfirmResult confirmResult = confirm_.drawAndPoll();
    if (confirmResult == ConfirmResult::Confirmed) {
        if (pendingConfirmAction == PendingConfirmAction::DiscardMatchChanges) {
            if (lastSyncedMatchConfig) {
                draft->killsToWin = lastSyncedMatchConfig->killsToWin;
                draft->maxPlayers = lastSyncedMatchConfig->maxPlayers;
            }
            if (lastSyncedDiscoverySettings) {
                draft->advertiseGlobal = lastSyncedDiscoverySettings->advertiseGlobal;
                draft->advertiseLan = lastSyncedDiscoverySettings->advertiseLan;
            }
            pendingGoToLobby = true;
        } else if (pendingConfirmAction == PendingConfirmAction::ShutdownServer) {
            lastHostError.clear();
            pendingShutdown = true;
        }
        pendingConfirmAction = PendingConfirmAction::None;
    } else if (confirmResult == ConfirmResult::Cancelled) {
        pendingConfirmAction = PendingConfirmAction::None;
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

bool MainMenu::consumeLaunchRequest()
{
    if (!pendingLaunch)
        return false;

    pendingLaunch = false;
    return true;
}

bool MainMenu::consumeShutdownRequest()
{
    if (!pendingShutdown)
        return false;

    pendingShutdown = false;
    return true;
}

bool MainMenu::consumeGoToLobbyRequest()
{
    if (!pendingGoToLobby)
        return false;

    pendingGoToLobby = false;
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

void MainMenu::setLaunchError(const std::string& error)
{
    lastHostError = error;
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

HostConfigState MainMenu::consumeDraftConfig() const
{
    return draftConfig();
}

bool MainMenu::canManageCurrentServer() const
{
    if (!client)
        return false;

    const auto latestLobbyState = client->getLatestLobbyState();
    if (!latestLobbyState)
        return false;

    const auto& [players, localId] = *latestLobbyState;
    return std::any_of(players.begin(), players.end(), [localId](const LobbyPlayer& player) {
        return player.id == localId && player.isHost;
    });
}

bool MainMenu::hasUnsavedServerChanges() const
{
    if (!draft || !lastSyncedMatchConfig || !lastSyncedDiscoverySettings)
        return false;

    return std::clamp(draft->killsToWin, 1, 100) != lastSyncedMatchConfig->killsToWin ||
           std::clamp(draft->maxPlayers, 2, 128) != lastSyncedMatchConfig->maxPlayers ||
           draft->advertiseGlobal != lastSyncedDiscoverySettings->advertiseGlobal ||
           draft->advertiseLan != lastSyncedDiscoverySettings->advertiseLan;
}

HostConfigState MainMenu::draftConfig() const
{
    if (!draft)
        return HostConfigState{
            .port = 9999,
            .useSpecificPort = false,
            .useLegacyTcp = false,
            .persistAfterClientExit = false,
            .advertiseGlobal = true,
            .advertiseLan = true,
            .serverName = std::string(server_name::k_default),
            .killsToWin = 25,
            .maxPlayers = 8,
        };

    HostConfigState result = *draft;
    result.port = std::clamp(result.port, 0, 65535);
    result.serverName = server_name::sanitize(result.serverName);
    result.killsToWin = std::clamp(result.killsToWin, 1, 100);
    result.maxPlayers = std::clamp(result.maxPlayers, 2, 128);
    return result;
}

bool MainMenu::updateServerSettings()
{
    if (!client || !draft)
        return false;

    const MatchConfig config{.killsToWin = std::clamp(draft->killsToWin, 1, 100),
                             .maxPlayers = std::clamp(draft->maxPlayers, 2, 128)};
    const DiscoverySettings discoverySettings{.advertiseGlobal = draft->advertiseGlobal,
                                              .advertiseLan = draft->advertiseLan};
    draft->killsToWin = config.killsToWin;
    draft->maxPlayers = config.maxPlayers;
    if (!client->sendMatchConfig(config)) {
        lastHostError = "Failed to send match settings update";
        return false;
    }
    if (!client->sendDiscoverySettings(discoverySettings)) {
        lastHostError = "Failed to send discovery settings update";
        return false;
    }

    lastHostError.clear();
    lastSyncedMatchConfig = config;
    lastSyncedDiscoverySettings = discoverySettings;
    return true;
}

void MainMenu::requestDiscardMatchChangesConfirm()
{
    pendingConfirmAction = PendingConfirmAction::DiscardMatchChanges;
    confirm_.open({.title = "Discard Server Settings?",
                   .message = "You have unsaved server setting changes. Discard them?",
                   .confirmText = "Discard",
                   .cancelText = "Cancel",
                   .confirmIsDanger = true});
}

void MainMenu::requestShutdownConfirm()
{
    pendingConfirmAction = PendingConfirmAction::ShutdownServer;
    confirm_.open({.title = "Shutdown Server?",
                   .message = "Stop this server for all connected players?",
                   .confirmText = "Shutdown",
                   .cancelText = "Cancel",
                   .confirmIsDanger = true});
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
        GlobalDiscoveryClient discoveryClient;
        std::vector<net::discovery::ServerInfo> servers;
        std::string error;
        const bool ok = discoveryClient.fetchServers(cfg, servers, error);
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
