/// @file App.cpp
/// @brief Manages main application lifecycle: initialization, event handling, main loop iteration, and cleanup.  Owns
/// the main window, renderer, and network client.
#include "App.hpp"

#include "SDL3/SDL_init.h"
#include "game/Game.hpp"
#include "host/HostedServer.hpp"
#include "menus/home/Home.hpp"
#include "menus/host/HostConfig.hpp"
#include "menus/lobby/Lobby.hpp"
#include "network/discovery/GlobalDiscoveryClient.hpp"
#include "renderer-new/GraphicsConfig.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <optional>
#include <string>

namespace
{
constexpr int k_joinConnectionTimeoutMs = 5000;

/// @brief Return a short log-friendly string for a ConnectError value.
const char* connectErrorLogName(ConnectError error)
{
    switch (error) {
    case ConnectError::None:
        return "none";
    case ConnectError::ResolveFailed:
        return "resolve failed";
    case ConnectError::ResolveTimedOut:
        return "resolve timed out";
    case ConnectError::CreateClientFailed:
        return "create client failed";
    case ConnectError::ConnectTimedOut:
        return "connect timed out";
    case ConnectError::ConnectFailed:
        return "connect failed";
    }

    return "unknown";
}

/// @brief Return a user-facing error message for a failed connection attempt.
const char* joinErrorMessage(ConnectError error)
{
    switch (error) {
    case ConnectError::ResolveFailed:
        return "Could not resolve server address";
    case ConnectError::ResolveTimedOut:
    case ConnectError::ConnectTimedOut:
        return "Connection timed out";
    default:
        return "Failed to connect to server";
    }
}
} // namespace

bool App::init()
{
    static constexpr const char* k_appName = "group2";
    SDL_SetAppMetadata(k_appName, "0.1.0", "com.cse125.group2");

    // Initialize SDL and network library
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        cleanup();
        return false;
    }

    if (!NET_Init()) {
        SDL_Log("NET_Init() failed: %s", SDL_GetError());
        cleanup();
        return false;
    }

    // Create window
    window = SDL_CreateWindow(k_appName, 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        cleanup();
        return false;
    }

    {
        const char* base = SDL_GetBasePath();
        std::string cfgPath = std::string(base ? base : "") + "config.toml";

        // Apply graphics backend selection BEFORE SDL_CreateGPUDevice runs in
        // Renderer::init.  SDL_GPU honours SDL_HINT_GPU_DRIVER at device
        // creation; if the requested driver is unavailable SDL falls back to
        // another supported one automatically.
        const GraphicsConfig gfxCfg = loadGraphicsConfig(cfgPath.c_str());
        if (const char* driver = gpuBackendHintString(gfxCfg.backend))
            SDL_SetHint(SDL_HINT_GPU_DRIVER, driver);

        networkConfig = loadNetworkConfig(cfgPath.c_str());
        developerConfig = loadDeveloperConfig(cfgPath.c_str());
        hostConfigState.port = networkConfig.serverNetwork.port;
        hostConfigState.useSpecificPort = false;
        hostConfigState.useLegacyTcp = false;
    }

    // Pull user-specific settings once; App owns the live copy while screens borrow it.
    userSettingsPath = user_settings::getPath();
    userSettings = user_settings::load(userSettingsPath);

    // ImGui context must exist before Renderer::init, which sets up the
    // SDL_GPU ImGui backend.  App owns the context lifetime so it survives
    // screen transitions (Lobby has no ImGui, Game uses it for DebugUI).
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextOwned = true;
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    }
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
        SDL_Log("ImGui_ImplSDL3_InitForSDLGPU failed");
        cleanup();
        return false;
    }

    if (!renderer.init(window)) {
        SDL_Log("Renderer initialization failed");
        cleanup();
        return false;
    }

    // Developer skip
    if (developerConfig.skipLobby) {
        const NetworkAddress clientNet = networkConfig.clientNetwork;
        const ConnectError connectError = client.init(clientNet.host.c_str(), clientNet.port, networkConfig.transport);
        if (connectError != ConnectError::None) {
            SDL_Log("Failed to connect to server: %s", connectErrorLogName(connectError));
            cleanup();
            return false;
        }
        AppContext ctx = screenContext();
        auto game = std::make_unique<Game>();
        if (!game->initDebugUI(ctx) || !game->init(ctx)) {
            game->quit();
            cleanup();
            return false;
        }
        screen_ = std::move(game);
        current = Screen::InGame;
    } else {
        AppContext ctx = screenContext();
        auto homeScreen = std::make_unique<Home>();
        if (!homeScreen->init(ctx)) {
            homeScreen->quit();
            cleanup();
            return false;
        }
        screen_ = std::move(homeScreen);
        current = Screen::Home;
    }

    return true;
}

SDL_AppResult App::event(SDL_Event* event)
{
    if (!screen_)
        return SDL_APP_FAILURE;
    return screen_->event(event);
}

SDL_AppResult App::iterate()
{
    if (!screen_)
        return SDL_APP_FAILURE;
    const SDL_AppResult result = screen_->iterate();
    if (result != SDL_APP_CONTINUE)
        return result;

    switch (current) {
    case Screen::Home: {
        auto home = dynamic_cast<Home*>(screen_.get());
        if (!home)
            break;
        if (auto joinRequest = home->consumeJoinRequest()) {
            std::string serverIp = joinRequest->serverIp;
            uint16_t serverPort = joinRequest->serverPort;
            std::optional<net::UdpSessionTransport::RelayConfig> relayConfig;
            if (joinRequest->globalServerId != 0 && networkConfig.discovery.enabled) {
                GlobalDiscoveryClient discovery;
                net::discovery::ServerInfo punchedServer;
                std::string punchError;
                const std::uint32_t clientNonce = net::discovery::randomNonce();
                net::RelayToken relayToken;
                if (discovery.requestHolePunch(networkConfig.discovery,
                                               joinRequest->globalServerId,
                                               clientNonce,
                                               punchedServer,
                                               punchError,
                                               networkConfig.discovery.connectPunchTimeoutMs,
                                               &relayToken))
                {
                    serverIp = punchedServer.host;
                    serverPort = punchedServer.gamePort;
                    relayConfig = net::UdpSessionTransport::RelayConfig{
                        .host = networkConfig.discovery.directoryHost,
                        .port = networkConfig.discovery.directoryUdpPort,
                        .serverId = joinRequest->globalServerId,
                        .clientNonce = clientNonce,
                        .relayToken = relayToken,
                        .enabled = true,
                    };
                } else if (!punchError.empty()) {
                    SDL_Log("Global punch assist failed for server %u: %s",
                            joinRequest->globalServerId,
                            punchError.c_str());
                }
            }
            SDL_Log("Attempting to join server at %s:%d...", serverIp.c_str(), serverPort);
            const ConnectError connectError = client.init(
                serverIp.c_str(), serverPort, networkConfig.transport, k_joinConnectionTimeoutMs, relayConfig);
            if (connectError != ConnectError::None) {
                SDL_Log("Failed to connect to server at %s:%d: %s",
                        serverIp.c_str(),
                        serverPort,
                        connectErrorLogName(connectError));
                home->setJoinError(joinErrorMessage(connectError));
            } else {
                SDL_Log("Successfully connected to server at %s:%d", serverIp.c_str(), serverPort);
                transitionTo(Screen::Lobby);
            }
        }

        if (home->consumeHostRequest()) {
            transitionTo(Screen::HostConfig);
        }
        break;
    }
    case Screen::HostConfig: {
        auto* hostConfig = dynamic_cast<HostConfig*>(screen_.get());
        if (!hostConfig)
            break;

        if (hostConfig->consumeLaunchRequest()) {
            HostConfigState config = hostConfig->draftConfig();
            hostConfigState = config;

            if (config.useLegacyTcp && !config.useSpecificPort) {
                hostConfig->setLaunchError("Legacy TCP requires a specific port");
                break;
            }

            std::string error;
            if (!hostedServer.start(config, error)) {
                hostConfig->setLaunchError(error.empty() ? "Failed to start hosted server" : error);
                break;
            }

            SDL_Log("Hosted server started on port %d, connecting client...", hostedServer.port());
            TransportConfig hostedTransport = networkConfig.transport;
            if (config.useLegacyTcp) {
                hostedTransport.useUdpSessions = false;
            }
            const ConnectError connectError = client.init("127.0.0.1", hostedServer.port(), hostedTransport);
            if (connectError != ConnectError::None) {
                SDL_Log("Failed to connect to hosted server: %s", connectErrorLogName(connectError));
                hostConfig->setLaunchError(joinErrorMessage(connectError));
                hostedServer.shutdown();
            } else {
                SDL_Log("Successfully connected to hosted server at 127.0.0.1:%d", hostedServer.port());
            }
        }

        if (hostConfig->consumeShutdownRequest()) {
            client.shutdown();
            if (hostedServer.isRunning()) {
                hostedServer.shutdown();
            }
        }

        if (hostConfig->consumeGoToLobbyRequest() && hostedServer.isRunning()) {
            transitionTo(Screen::Lobby);
            break;
        }

        if (hostConfig->consumeBackToHomeRequest()) {
            transitionTo(Screen::Home);
        }
        break;
    }
    case Screen::Lobby: {
        auto* lobby = dynamic_cast<Lobby*>(screen_.get());
        if (!lobby)
            break;

        if (lobby->consumeReturnToHostConfig()) {
            transitionTo(Screen::HostConfig);
            break;
        }

        if (lobby->consumeReturnToMenu()) {
            const bool showServerShutdownNotice = lobby->consumeServerShutdownNotice();
            client.shutdown();
            if (hostedServer.isRunning()) {
                hostedServer.shutdown();
            }

            transitionTo(Screen::Home);
            if (showServerShutdownNotice) {
                showHomePopupMessage("Server shutdown");
            }
            break;
        }

        if (lobby->shouldStartMatch()) {
            lobby->consumeStartMatchState();
            transitionTo(Screen::InGame);
        }
        break;
    }
    case Screen::InGame: {
        auto* game = dynamic_cast<Game*>(screen_.get());
        if (!game)
            break;

        if (game->consumeReturnToMainMenu()) {
            const bool showServerShutdownNotice = game->consumeServerShutdownNotice();
            client.shutdown();
            transitionTo(Screen::Home);
            if (showServerShutdownNotice) {
                showHomePopupMessage("Server shutdown");
            }
            break;
        }

        if (developerConfig.skipLobby)
            break;
        if (game->shouldReturnToLobby()) {
            transitionTo(Screen::Lobby);
        }
        break;
    }
    default:
        break;
    }

    return result;
}

void App::quit()
{
    cleanup();
}

void App::transitionTo(Screen next)
{
    if (screen_ && next == current)
        return;

    if (screen_) {
        screen_->quit();
        screen_.reset();
    }

    AppContext ctx = screenContext();
    switch (next) {
    case Screen::InGame: {
        auto game = std::make_unique<Game>();
        if (game->initDebugUI(ctx) && game->init(ctx)) {
            screen_ = std::move(game);
            current = next;
        } else {
            game->quit();
        }
        break;
    }
    case Screen::Lobby: {
        auto lobby = std::make_unique<Lobby>();
        if (lobby->init(ctx)) {
            screen_ = std::move(lobby);
            current = next;
        } else {
            lobby->quit();
        }
        break;
    }
    case Screen::HostConfig: {
        auto hostConfig = std::make_unique<HostConfig>();
        if (hostConfig->init(ctx)) {
            screen_ = std::move(hostConfig);
            current = next;
        } else {
            hostConfig->quit();
        }
        break;
    }
    case Screen::Home: {
        auto homeScreen = std::make_unique<Home>();
        if (homeScreen->init(ctx)) {
            screen_ = std::move(homeScreen);
            current = next;
        } else {
            homeScreen->quit();
        }
        break;
    }
    default:
        break;
    }
}

void App::cleanup()
{
    if (screen_) {
        screen_->quit();
    }
    if (!userSettingsPath.empty()) {
        user_settings::save(userSettingsPath, userSettings);
    }
    if (hostedServer.isRunning()) {
        hostedServer.shutdown();
    }
    client.shutdown();
    renderer.quit();
    if (screen_) {
        screen_->shutdownAfterRenderer();
        screen_.reset();
    }
    if (imguiContextOwned) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiContextOwned = false;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    NET_Quit();
    SDL_Quit();
}

AppContext App::screenContext()
{
    return AppContext{
        .window = *window,
        .renderer = renderer,
        .client = client,
        .hostedServer = hostedServer,
        .hostConfigState = hostConfigState,
        .networkConfig = networkConfig,
        .developerConfig = developerConfig,
        .userSettings = userSettings,
        .userSettingsPath = userSettingsPath,
    };
}

void App::showHomePopupMessage(const std::string& message)
{
    auto* home = dynamic_cast<Home*>(screen_.get());
    if (home) {
        home->setPopupMessage(message);
    }
}
