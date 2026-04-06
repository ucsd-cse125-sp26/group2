#pragma once

#include <SDL3/SDL.h>

class Renderer
{
public:
    bool init(SDL_Renderer* renderer);
    void drawFrame();
    void quit();

private:
    SDL_Renderer* renderer = nullptr;
};