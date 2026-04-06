#ifndef GROUP2_MAIN_H
#define GROUP2_MAIN_H

#include "ecs/Registry.hpp"

#include <SDL3/SDL.h>

struct AppState
{
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Registry registry;
};

#endif // GROUP2_MAIN_H
