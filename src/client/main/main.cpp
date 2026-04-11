/// @file main.cpp
/// @brief Client application entry point using SDL callback-driven lifecycle.

#define SDL_MAIN_USE_CALLBACKS // For using callbacks instead of a main() entrypoint

#include "game/Game.hpp"

#include <SDL3/SDL_main.h>

/// @brief Initialise the client application and create the Game instance.
/// @param appstate Output pointer that receives the Game object.
/// @param argc     Unused argument count.
/// @param argv     Unused argument vector.
/// @return SDL_APP_CONTINUE on success, SDL_APP_FAILURE on error.
SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
    auto* game = new Game();

    if (!game->init())
        return SDL_APP_FAILURE;

    *appstate = game;
    return SDL_APP_CONTINUE;
}

/// @brief Forward SDL events to the Game instance.
/// @param appstate The Game object.
/// @param event    The incoming SDL event.
/// @return Application continuation result.
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    return static_cast<Game*>(appstate)->event(event);
}

/// @brief Run one iteration of the client game loop.
/// @param appstate The Game object.
/// @return Application continuation result.
SDL_AppResult SDL_AppIterate(void* appstate)
{
    return static_cast<Game*>(appstate)->iterate();
}

/// @brief Clean up and delete the Game instance on exit.
/// @param appstate The Game object.
/// @param result   Unused exit result code.
void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* game = static_cast<Game*>(appstate);
    game->quit();
    delete game;
}
