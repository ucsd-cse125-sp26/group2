#pragma once
#include "IScreen.hpp"
#include "network/Client.hpp"
#include "network/NetworkConfig.hpp"
#include "renderer-new/NewRenderer.hpp"

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
    NewRenderer renderer;
    NetworkConfig networkConfig;
    Client client;

    Screen current = Screen::Lobby;
    std::unique_ptr<IScreen> screen_;
    bool imguiContextOwned = false;

    void cleanup();
};
