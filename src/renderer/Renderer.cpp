#include "Renderer.hpp"

bool Renderer::init(SDL_Renderer* renderer)
{
    this->renderer = renderer;
    return true;
}

void Renderer::drawFrame()
{
    SDL_FRect rect;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    rect.x = rect.y = 100;
    rect.w = 440;
    rect.h = 280;
    SDL_RenderFillRect(renderer, &rect);

    SDL_RenderPresent(renderer);
}

void Renderer::quit()
{
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
}
