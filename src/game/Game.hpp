#pragma once

#include "ecs/Registry.hpp"
#include "renderer/Renderer.hpp"

#include <SDL3/SDL.h>

class Game
{
public:
    bool init();
    SDL_AppResult event(SDL_Event* event);
    SDL_AppResult iterate();
    void quit();

private:
    SDL_Window* window;
    Renderer* renderer;
    Registry registry;
};