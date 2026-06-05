/// @file App.cpp
/// @brief Manages main application lifecycle: initialization, event handling, main loop iteration, and cleanup.  Owns
/// the main window, renderer, and network client.
#include "App.hpp"

#include "SDL3/SDL_init.h"
#include "game/Game.hpp"
#include "host/HostedServer.hpp"
#include "menus/MenuTheme.hpp"
#include "menus/host/HostConfig.hpp"
#include "menus/loading/LoadingScreen.hpp"
#include "menus/lobby/Lobby.hpp"
#include "menus/main/MainMenu.hpp"
#include "menus/postmatch/PostMatchScoreboard.hpp"
#include "menus/settings/SettingsScreen.hpp"
#include "menus/title/TitleScreen.hpp"
#include "network/discovery/GlobalDiscoveryClient.hpp"
#include "renderer-new/GraphicsConfig.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <backends/imgui_impl_sdl3.h>
#include <chrono>
#include <imgui.h>
#include <optional>
#include <string>

namespace
{
constexpr int k_joinConnectionTimeoutMs = 5000;
constexpr int k_hostedShutdownPollMs = 25;
constexpr int k_hostedShutdownTimeoutMs = 1000;

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
    case ConnectError::LobbyFull:
        return "lobby full";
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
    case ConnectError::LobbyFull:
        return "Lobby full";
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
    window = SDL_CreateWindow(
        k_appName, 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_FULLSCREEN);
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
        hostConfigState.advertiseGlobal = networkConfig.discovery.advertiseServer;
        hostConfigState.advertiseLan = networkConfig.discovery.lanBroadcastEnabled;
        hostConfigState.serverName = networkConfig.discovery.serverName;
        hostConfigState.maxPlayers = networkConfig.discovery.maxPlayers;
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
        // Let a connected controller drive menus/debug UI (pause menu, etc.).
        // The SDL3 backend feeds gamepad state into ImGui nav each frame.
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
    ImGui::StyleColorsDark();
    menu_theme::applyStyle();
    menu_theme::loadFonts();
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

    sfxSystem.setPlaybackDeviceName(userSettings.audioOutputDeviceName);
    if (!sfxSystem.init()) {
        SDL_Log("[client] SfxSystem init failed (non-fatal — music and sound effects disabled)");
    }
    menu_theme::setSfxSystem(&sfxSystem);
    applyAudioSettings();
    previousAudioCounter_ = SDL_GetPerformanceCounter();

    // Developer skip
    if (developerConfig.skipLobby) {
        const NetworkAddress clientNet = networkConfig.clientNetwork;
        const ConnectError connectError =
            client.init(clientNet.host.c_str(), clientNet.port, networkConfig.transport, k_joinConnectionTimeoutMs);
        if (connectError != ConnectError::None) {
            SDL_Log("Failed to connect to server: %s", connectErrorLogName(connectError));
            cleanup();
            return false;
        }
        AppContext ctx = screenContext();
        auto loading = std::make_unique<LoadingScreen>();
        if (!loading->init(ctx)) {
            loading->quit();
            cleanup();
            return false;
        }
        screen_ = std::move(loading);
        current = Screen::Loading;
    } else {
        AppContext ctx = screenContext();
        auto titleScreen = std::make_unique<TitleScreen>();
        if (!titleScreen->init(ctx)) {
            titleScreen->quit();
            cleanup();
            return false;
        }
        screen_ = std::move(titleScreen);
        current = Screen::TitleScreen;
    }

    updateBackgroundMusic();

    return true;
}

SDL_AppResult App::event(SDL_Event* event)
{
    if (!screen_)
        return SDL_APP_FAILURE;
    if (event)
        sfxSystem.handleEvent(*event);
    return screen_->event(event);
}

SDL_AppResult App::iterate()
{
    if (!screen_)
        return SDL_APP_FAILURE;

    const Uint64 now = SDL_GetPerformanceCounter();
    const Uint64 frequency = SDL_GetPerformanceFrequency();
    const float audioDt = previousAudioCounter_ != 0 && frequency != 0
                              ? static_cast<float>(now - previousAudioCounter_) / static_cast<float>(frequency)
                              : 0.0f;
    previousAudioCounter_ = now;
    applyAudioSettings();
    updateBackgroundMusic();
    if (current != Screen::InGame)
        sfxSystem.update(audioDt);

    const SDL_AppResult result = screen_->iterate();
    if (result != SDL_APP_CONTINUE)
        return result;
    pollJoinAttempt();

    switch (current) {
    case Screen::TitleScreen: {
        auto* titleScreen = dynamic_cast<TitleScreen*>(screen_.get());
        if (!titleScreen)
            break;

        if (titleScreen->consumeExitRequest()) {
            return SDL_APP_SUCCESS;
        }
        if (titleScreen->consumePlayRequest()) {
            nextMainMenuTab_ = ServerBrowserTab::LocalListing;
            transitionTo(Screen::MainMenu);
            break;
        }
        if (titleScreen->consumeHostRequest()) {
            nextMainMenuTab_ = ServerBrowserTab::HostConfig;
            transitionTo(Screen::MainMenu);
            break;
        }
        if (titleScreen->consumeSettingsRequest()) {
            settingsReturnScreen_ = Screen::TitleScreen;
            transitionTo(Screen::Settings);
            break;
        }
        break;
    }
    case Screen::Settings: {
        auto* settings = dynamic_cast<SettingsScreen*>(screen_.get());
        if (!settings)
            break;

        if (settings->consumeBackRequest()) {
            transitionTo(settingsReturnScreen_);
            break;
        }
        break;
    }
    case Screen::MainMenu: {
        auto* mainMenu = dynamic_cast<MainMenu*>(screen_.get());
        if (!mainMenu)
            break;
        if (mainMenu->consumeReturnToTitleScreenRequest()) {
            transitionTo(Screen::TitleScreen);
            break;
        }
        if (mainMenu->consumeExitRequest()) {
            return SDL_APP_SUCCESS;
        }
        if (auto joinRequest = mainMenu->consumeJoinRequest()) {
            startJoinAttempt(*joinRequest);
        }

        if (mainMenu->consumeLaunchRequest()) {
            if (client.isConnected()) {
                client.shutdown();
            }

            HostConfigState config = mainMenu->consumeDraftConfig();
            hostConfigState = config;

            if (config.useLegacyTcp && !config.useSpecificPort) {
                mainMenu->setLaunchError("Legacy TCP requires a specific port");
                break;
            }

            std::string error;
            if (!hostedServer.start(config, error)) {
                mainMenu->setLaunchError(error.empty() ? "Failed to start hosted server" : error);
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
                mainMenu->setLaunchError(joinErrorMessage(connectError));
                hostedServer.shutdown();
            } else {
                SDL_Log("Successfully connected to hosted server at 127.0.0.1:%d", hostedServer.port());
                currentServerName = config.serverName;
                currentServerIp = "127.0.0.1";
                currentServerPort = hostedServer.port();
                menu_theme::playUiSound(UiSoundAction::Success);
            }
        }

        if (mainMenu->consumeShutdownRequest()) {
            shutdownHostedServerGracefully();
            client.shutdown();
        }

        if (mainMenu->consumeGoToLobbyRequest() && (hostedServer.isRunning() || client.isConnected())) {
            menu_theme::playUiSound(UiSoundAction::Success);
            transitionTo(Screen::Lobby);
        }
        break;
    }
    case Screen::HostConfig: {
        auto* hostConfig = dynamic_cast<HostConfig*>(screen_.get());
        if (!hostConfig)
            break;

        if (hostConfig->consumeLaunchRequest()) {
            if (client.isConnected()) {
                client.shutdown();
            }

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
                currentServerName = config.serverName;
                currentServerIp = "127.0.0.1";
                currentServerPort = hostedServer.port();
            }
        }

        if (hostConfig->consumeShutdownRequest()) {
            shutdownHostedServerGracefully();
            client.shutdown();
        }

        if (hostConfig->consumeExitRequest()) {
            return SDL_APP_SUCCESS;
        }

        if (hostConfig->consumeGoToLobbyRequest() && (hostedServer.isRunning() || client.isConnected())) {
            transitionTo(Screen::Lobby);
            break;
        }

        if (hostConfig->consumeBackToMainMenuRequest()) {
            if (hostedServer.isRunning()) {
                shutdownHostedServerGracefully();
            }
            client.shutdown();
            transitionTo(Screen::MainMenu);
        }
        break;
    }
    case Screen::Lobby: {
        auto* lobby = dynamic_cast<Lobby*>(screen_.get());
        if (!lobby)
            break;

        if (lobby->consumeReturnToHostConfig()) {
            nextMainMenuTab_ = ServerBrowserTab::HostConfig;
            transitionTo(Screen::MainMenu);
            break;
        }

        if (lobby->consumeExitRequest()) {
            return SDL_APP_SUCCESS;
        }

        if (lobby->consumeReturnToMenu()) {
            const bool showServerShutdownNotice = lobby->consumeServerShutdownNotice();
            if (hostedServer.isRunning()) {
                shutdownHostedServerGracefully();
            }
            client.shutdown();

            if (showServerShutdownNotice) {
                nextMainMenuTab_ = ServerBrowserTab::LocalListing;
                transitionTo(Screen::MainMenu);
                showMainMenuPopupMessage("Server shutdown");
            } else {
                nextMainMenuTab_ = ServerBrowserTab::LocalListing;
                transitionTo(Screen::MainMenu);
            }
            break;
        }

        if (lobby->shouldStartMatch()) {
            lobby->consumeStartMatchState();
            if (const auto latestServerName = client.getLatestServerName();
                latestServerName && !latestServerName->empty())
            {
                currentServerName = *latestServerName;
            }
            transitionTo(Screen::Loading);
        }
        break;
    }
    case Screen::Loading: {
        auto* loading = dynamic_cast<LoadingScreen*>(screen_.get());
        if (!loading)
            break;

        if (loading->readyToStartGame()) {
            transitionTo(Screen::InGame);
        }
        break;
    }
    case Screen::PostMatch: {
        auto* postMatch = dynamic_cast<PostMatchScoreboard*>(screen_.get());
        if (!postMatch)
            break;

        if (postMatch->consumeReturnToMenu()) {
            const bool showServerShutdownNotice = postMatch->consumeServerShutdownNotice();
            if (hostedServer.isRunning()) {
                shutdownHostedServerGracefully();
            }
            client.shutdown();
            if (showServerShutdownNotice) {
                nextMainMenuTab_ = ServerBrowserTab::LocalListing;
                transitionTo(Screen::MainMenu);
                showMainMenuPopupMessage("Server shutdown");
            } else {
                nextMainMenuTab_ = ServerBrowserTab::LocalListing;
                transitionTo(Screen::MainMenu);
            }
            break;
        }

        if (postMatch->consumeExitRequest()) {
            return SDL_APP_SUCCESS;
        }

        if (postMatch->consumeReturnToLobby()) {
            transitionTo(Screen::Lobby);
            break;
        }
        break;
    }
    case Screen::InGame: {
        auto* game = dynamic_cast<Game*>(screen_.get());
        if (!game)
            break;

        if (game->consumeReturnToMainMenu()) {
            const bool showServerShutdownNotice = game->consumeServerShutdownNotice();
            if (hostedServer.isRunning()) {
                shutdownHostedServerGracefully();
            }
            client.shutdown();
            if (showServerShutdownNotice) {
                nextMainMenuTab_ = ServerBrowserTab::LocalListing;
                transitionTo(Screen::MainMenu);
                showMainMenuPopupMessage("Server shutdown");
            } else {
                nextMainMenuTab_ = ServerBrowserTab::LocalListing;
                transitionTo(Screen::MainMenu);
            }
            break;
        }

        if (developerConfig.skipLobby)
            break;
        if (auto postMatchResult = game->consumePostMatchResult()) {
            pendingPostMatchResult_ = std::move(*postMatchResult);
            transitionTo(Screen::PostMatch);
            break;
        }
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
    case Screen::TitleScreen: {
        auto titleScreen = std::make_unique<TitleScreen>();
        if (titleScreen->init(ctx)) {
            screen_ = std::move(titleScreen);
            current = next;
        } else {
            titleScreen->quit();
        }
        break;
    }
    case Screen::InGame: {
        auto game = std::make_unique<Game>();
        if (game->initDebugUI(ctx) && game->init(ctx)) {
            screen_ = std::move(game);
            current = next;
        } else {
            game->quit();
            client.shutdown();
            auto mainMenu = std::make_unique<MainMenu>();
            nextMainMenuTab_ = ServerBrowserTab::LocalListing;
            if (mainMenu->init(ctx, nextMainMenuTab_)) {
                mainMenu->setPopupMessage("Failed to initialize match");
                screen_ = std::move(mainMenu);
                current = Screen::MainMenu;
            } else {
                mainMenu->quit();
            }
        }
        break;
    }
    case Screen::Loading: {
        auto loading = std::make_unique<LoadingScreen>();
        if (loading->init(ctx)) {
            screen_ = std::move(loading);
            current = next;
        } else {
            loading->quit();
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
    case Screen::PostMatch: {
        if (!pendingPostMatchResult_) {
            next = Screen::Lobby;
            auto lobby = std::make_unique<Lobby>();
            if (lobby->init(ctx)) {
                screen_ = std::move(lobby);
                current = next;
            } else {
                lobby->quit();
            }
            break;
        }

        auto postMatch = std::make_unique<PostMatchScoreboard>();
        if (postMatch->init(ctx, std::move(*pendingPostMatchResult_))) {
            pendingPostMatchResult_.reset();
            screen_ = std::move(postMatch);
            current = next;
        } else {
            pendingPostMatchResult_.reset();
            postMatch->quit();
        }
        break;
    }
    case Screen::Settings: {
        auto settings = std::make_unique<SettingsScreen>();
        if (settings->init(ctx)) {
            screen_ = std::move(settings);
            current = next;
        } else {
            settings->quit();
        }
        break;
    }
    case Screen::HostConfig: {
        auto mainMenu = std::make_unique<MainMenu>();
        if (mainMenu->init(ctx, ServerBrowserTab::HostConfig)) {
            screen_ = std::move(mainMenu);
            current = Screen::MainMenu;
        } else {
            mainMenu->quit();
        }
        break;
    }
    case Screen::MainMenu: {
        auto mainMenu = std::make_unique<MainMenu>();
        if (mainMenu->init(ctx, nextMainMenuTab_)) {
            screen_ = std::move(mainMenu);
            current = next;
        } else {
            mainMenu->quit();
        }
        break;
    }
    default:
        break;
    }

    updateBackgroundMusic();
}

void App::cleanup()
{
    if (screen_) {
        screen_->quit();
    }
    waitForJoinAttempt();
    if (!userSettingsPath.empty()) {
        user_settings::save(userSettingsPath, userSettings);
    }
    if (hostedServer.isRunning()) {
        shutdownHostedServerGracefully();
    }
    client.shutdown();
    sfxSystem.stopMusic();
    sfxSystem.quit();
    menu_theme::releaseBackground(renderer.getDevice());
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
        .sfxSystem = sfxSystem,
        .hostedServer = hostedServer,
        .hostConfigState = hostConfigState,
        .networkConfig = networkConfig,
        .developerConfig = developerConfig,
        .userSettings = userSettings,
        .userSettingsPath = userSettingsPath,
        .currentServerName = currentServerName,
        .currentServerIp = currentServerIp,
        .currentServerPort = currentServerPort,
    };
}

void App::applyAudioSettings()
{
    if (!sfxSystem.isInitialized())
        return;

    sfxSystem.setPlaybackDeviceName(userSettings.audioOutputDeviceName);
    sfxSystem.setCategoryVolume(SfxCategory::Music, userSettings.musicVolume);
    sfxSystem.setCategoryVolume(SfxCategory::Weapons, userSettings.sfxVolume);
    sfxSystem.setCategoryVolume(SfxCategory::Impacts, userSettings.sfxVolume);
    sfxSystem.setCategoryVolume(SfxCategory::Player, userSettings.sfxVolume);
    sfxSystem.setCategoryVolume(SfxCategory::Footsteps, userSettings.sfxVolume);
    sfxSystem.setCategoryVolume(SfxCategory::Voice, userSettings.sfxVolume);
    sfxSystem.setCategoryVolume(SfxCategory::UI, userSettings.sfxVolume);
}

void App::updateBackgroundMusic()
{
    if (!sfxSystem.isInitialized())
        return;

    const SfxId desiredMusic = current == Screen::InGame ? SfxId::GameMusic : SfxId::MenuMusic;
    sfxSystem.playMusic(desiredMusic);
}

void App::showMainMenuPopupMessage(const std::string& message)
{
    auto* mainMenu = dynamic_cast<MainMenu*>(screen_.get());
    if (mainMenu) {
        mainMenu->setPopupMessage(message);
    }
}

void App::startJoinAttempt(const JoinRequest& request)
{
    if (joinAttempt_.valid())
        return;

    joinAttemptLabel_ = request.serverName.empty() ? request.serverIp : request.serverName;
    if (auto* mainMenu = dynamic_cast<MainMenu*>(screen_.get())) {
        mainMenu->setJoinInProgress(true, joinAttemptLabel_);
    }

    NetworkConfig cfg = networkConfig;
    Client& joinClient = client;
    joinAttempt_ = std::async(std::launch::async, [request, cfg, &joinClient]() mutable {
        JoinAttemptResult result{
            .error = ConnectError::ConnectFailed,
            .serverIp = request.serverIp,
            .serverPort = request.serverPort,
            .serverName = request.serverName,
        };

        std::optional<net::UdpSessionTransport::RelayConfig> relayConfig;
        std::optional<net::UdpSessionTransport::PunchAssist> punchAssist;
        if (request.globalServerId != 0 && cfg.discovery.enabled) {
            GlobalDiscoveryClient discovery;
            net::discovery::ServerInfo punchedServer;
            std::string punchError;
            const std::uint32_t clientNonce = net::discovery::randomNonce();
            net::RelayToken relayToken;
            if (discovery.requestHolePunch(cfg.discovery,
                                           request.globalServerId,
                                           clientNonce,
                                           punchedServer,
                                           punchError,
                                           cfg.discovery.connectPunchTimeoutMs,
                                           &relayToken))
            {
                result.serverIp = punchedServer.udpHost.empty() ? punchedServer.host : punchedServer.udpHost;
                result.serverPort = punchedServer.udpPort != 0 ? punchedServer.udpPort : punchedServer.gamePort;
                SDL_Log("Global punch assist selected public UDP endpoint %s:%u for server %u",
                        result.serverIp.c_str(),
                        result.serverPort,
                        request.globalServerId);
                punchAssist = net::UdpSessionTransport::PunchAssist{
                    .directoryHost = cfg.discovery.directoryHost,
                    .directoryPort = cfg.discovery.directoryUdpPort,
                    .request = net::discovery::encodePunchRequest(
                        {.serverId = request.globalServerId, .clientNonce = clientNonce}),
                    .enabled = true,
                };
                if (!cfg.transport.noRelay) {
                    relayConfig = net::UdpSessionTransport::RelayConfig{
                        .host = cfg.discovery.directoryHost,
                        .port = cfg.discovery.directoryUdpPort,
                        .serverId = request.globalServerId,
                        .clientNonce = clientNonce,
                        .relayToken = relayToken,
                        .enabled = true,
                    };
                } else {
                    SDL_Log("Global punch assist succeeded for server %u; relay disabled, trying direct UDP only",
                            request.globalServerId);
                }
            } else if (!punchError.empty()) {
                SDL_Log("Global punch assist failed for server %u: %s", request.globalServerId, punchError.c_str());
            }
        }

        SDL_Log("Attempting to join server at %s:%d...", result.serverIp.c_str(), result.serverPort);
        result.error = joinClient.init(result.serverIp.c_str(),
                                       result.serverPort,
                                       cfg.transport,
                                       k_joinConnectionTimeoutMs,
                                       relayConfig,
                                       punchAssist);
        return result;
    });
}

void App::pollJoinAttempt()
{
    if (!joinAttempt_.valid())
        return;
    if (joinAttempt_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    JoinAttemptResult result = joinAttempt_.get();
    if (auto* mainMenu = dynamic_cast<MainMenu*>(screen_.get())) {
        mainMenu->setJoinInProgress(false);
    }
    joinAttemptLabel_.clear();

    if (result.error != ConnectError::None) {
        SDL_Log("Failed to connect to server at %s:%d: %s",
                result.serverIp.c_str(),
                result.serverPort,
                connectErrorLogName(result.error));
        if (auto* mainMenu = dynamic_cast<MainMenu*>(screen_.get())) {
            mainMenu->setJoinError(joinErrorMessage(result.error));
        }
        return;
    }

    SDL_Log("Successfully connected to server at %s:%d", result.serverIp.c_str(), result.serverPort);
    currentServerName = result.serverName.empty() ? result.serverIp : result.serverName;
    currentServerIp = result.serverIp;
    currentServerPort = result.serverPort;
    menu_theme::playUiSound(UiSoundAction::Success);
    transitionTo(Screen::Lobby);
}

void App::waitForJoinAttempt()
{
    if (!joinAttempt_.valid())
        return;
    const JoinAttemptResult result = joinAttempt_.get();
    if (result.error != ConnectError::None) {
        SDL_Log("Join attempt ended during shutdown: %s", connectErrorLogName(result.error));
    }
    joinAttemptLabel_.clear();
}

bool App::shutdownHostedServerGracefully()
{
    if (!hostedServer.isRunning()) {
        hostedServer.clearSession();
        return true;
    }

    const bool requested = client.isConnected() && client.sendServerShutdown();
    if (requested) {
        for (int waitedMs = 0; waitedMs < k_hostedShutdownTimeoutMs; waitedMs += k_hostedShutdownPollMs) {
            if (!hostedServer.isRunning()) {
                hostedServer.clearSession();
                return true;
            }
            client.poll();
            SDL_Delay(k_hostedShutdownPollMs);
        }
    }

    hostedServer.shutdown();
    hostedServer.clearSession();
    return false;
}
