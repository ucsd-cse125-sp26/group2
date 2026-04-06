#define SDL_MAIN_USE_CALLBACKS // For using callbacks instead of a main() entrypoint
#include "game/Game.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// ---------------------------------------------------------------------------
// SDL3 app callbacks
// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
    auto* game = new Game();

    if (!game->init())
        return SDL_APP_FAILURE;

    *appstate = game;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    return static_cast<Game*>(appstate)->event(event);
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    return static_cast<Game*>(appstate)->iterate();
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* game = static_cast<Game*>(appstate);
    game->quit();
    delete game;
}
