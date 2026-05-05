#pragma once
#include "IScreen.hpp"
#include "network/Client.hpp"
#include "network/NetworkConfig.hpp"

#ifdef USE_HYBRID_RENDERER
#include "renderer/HybridRenderer.hpp"
using AppRenderer = HybridRenderer;
#else
#include "renderer/Renderer.hpp"
using AppRenderer = Renderer;
#endif

#include <SDL3/SDL.h>

#include <memory>

class App
{
public:
    bool init();
    SDL_AppResult event(SDL_Event* event);
    SDL_AppResult iterate();
    void quit();

    enum class Screen
    {
        Lobby,
        InGame
    };
    void transitionTo(Screen next);

private:
    SDL_Window* window = nullptr;
    AppRenderer renderer;
    NetworkConfig networkConfig;
    Client client;

    Screen current = Screen::InGame;
    std::unique_ptr<IScreen> screen_;

    void cleanup();
};
