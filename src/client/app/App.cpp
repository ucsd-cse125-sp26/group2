/// @file App.cpp
/// @brief Manages main application lifecycle: initialization, event handling, main loop iteration, and cleanup.  Owns
/// the main window, renderer, and network client.
#include "App.hpp"

#include "SDL3/SDL_init.h"
#include "game/Game.hpp"
#include "renderer/GraphicsConfig.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>
#include <string>

bool App::init()
{
#ifdef USE_HYBRID_RENDERER
    static constexpr const char* k_appName = "group2";
#else
    static constexpr const char* k_appName = "client";
#endif
    SDL_SetAppMetadata(k_appName, "0.1.0", "com.cse125.group2");

    // Initialize SDL and network library
    if (!SDL_Init(SDL_INIT_VIDEO)) {
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
    }

    auto game = std::make_unique<Game>();
    if (!game->initDebugUI(window)) {
        game->quit();
        cleanup();
        return false;
    }
    Game* gameScreen = game.get();
    screen_ = std::move(game);

    // Create renderer after the Game-owned ImGui context exists.
    if (!renderer.init(window)) {
        SDL_Log("Renderer initialization failed");
        cleanup();
        return false;
    }

    // Create network
    const NetworkAddress clientNet = networkConfig.clientNetwork;
    if (!client.init(clientNet.host.c_str(), clientNet.port, networkConfig.transport)) {
        SDL_Log("Failed to connect to server");
        cleanup();
        return false;
    }

    if (!gameScreen->init(&renderer, window, &client)) {
        cleanup();
        return false;
    }
    current = Screen::InGame;

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
    return screen_->iterate();
}

void App::quit()
{
    cleanup();
}

void App::transitionTo(Screen next)
{
    if (screen_ && next == current)
        return;

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
    case Screen::Lobby:
        SDL_Log("Lobby screen transition requested, but Lobby is not implemented");
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
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    NET_Quit();
    SDL_Quit();
}
