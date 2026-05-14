/// @file App.cpp
/// @brief Manages main application lifecycle: initialization, event handling, main loop iteration, and cleanup.  Owns
/// the main window, renderer, and network client.
#include "App.hpp"

#include "SDL3/SDL_init.h"
#include "game/Game.hpp"
#include "menus/home/Home.hpp"
#include "menus/lobby/Lobby.hpp"
#include "renderer-new/GraphicsConfig.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <string>

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
        if (!client.init(clientNet.host.c_str(), clientNet.port, networkConfig.transport)) {
            SDL_Log("Failed to connect to server");
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
        if (!homeScreen->init(&renderer, window)) {
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
            SDL_Log("Attempting to join server at %s:%d...", serverIp.c_str(), serverPort);
            if (!client.init(serverIp.c_str(), serverPort, networkConfig.transport)) {
                SDL_Log("Failed to connect to server at %s:%d", serverIp.c_str(), serverPort);
                home->setJoinError("Failed to connect to server");
            } else {
                // TODO: Timeout if takes too long
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
        if (homeScreen->init(&renderer, window)) {
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
