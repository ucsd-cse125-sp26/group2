/// @file App.cpp
/// @brief Manages main application lifecycle: initialization, event handling, main loop iteration, and cleanup.  Owns
/// the main window, renderer, and network client.
#include "App.hpp"

#include "SDL3/SDL_init.h"
#include "game/Game.hpp"
#include "menus/home/Home.hpp"
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
    }

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
        auto game = std::make_unique<Game>();
        if (!game->initDebugUI(window) || !game->init(&renderer, window, &client)) {
            game->quit();
            cleanup();
            return false;
        }
        screen_ = std::move(game);
        current = Screen::InGame;
    } else {
        auto homeScreen = std::make_unique<Home>();
        if (!homeScreen->init(&renderer, window, networkConfig.discovery)) {
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
                std::uint64_t relayToken = 0;
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
        break;
    }
    case Screen::Lobby: {
        auto* lobby = dynamic_cast<Lobby*>(screen_.get());
        if (!lobby)
            break;

        if (lobby->consumeReturnToMenu()) {
            client.shutdown();
            transitionTo(Screen::Home);
            break;
        }

        if (lobby->shouldStartMatch()) {
            lobby->consumeStartMatchState();
            transitionTo(Screen::InGame);
        }
        break;
    }
    case Screen::InGame:
        if (developerConfig.skipLobby)
            break;
        if (auto* game = dynamic_cast<Game*>(screen_.get()); game != nullptr && game->shouldReturnToLobby()) {
            transitionTo(Screen::Lobby);
        }
        break;
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

    switch (next) {
    case Screen::InGame: {
        auto game = std::make_unique<Game>();
        if (game->initDebugUI(window) && game->init(&renderer, window, &client)) {
            screen_ = std::move(game);
            current = next;
        } else {
            game->quit();
        }
        break;
    }
    case Screen::Lobby: {
        auto lobby = std::make_unique<Lobby>();
        if (lobby->init(&renderer, window, &client)) {
            screen_ = std::move(lobby);
            current = next;
        } else {
            lobby->quit();
        }
        break;
    }
    case Screen::Home: {
        auto homeScreen = std::make_unique<Home>();
        if (homeScreen->init(&renderer, window, networkConfig.discovery)) {
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
