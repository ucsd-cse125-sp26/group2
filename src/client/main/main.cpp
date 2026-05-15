/// @file main.cpp
/// @brief Client application entry point using SDL callback-driven lifecycle.

#define SDL_MAIN_USE_CALLBACKS // For using callbacks instead of a main() entrypoint

#include "app/App.hpp"

#include <SDL3/SDL_main.h>

/// @brief Initialise the client application and create the App instance.
/// @param appstate Output pointer that receives the App object.
/// @param argc     Unused argument count.
/// @param argv     Unused argument vector.
/// @return SDL_APP_CONTINUE on success, SDL_APP_FAILURE on error.
SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
    auto* app = new App();

    if (!app->init()) {
        delete app;
        return SDL_APP_FAILURE;
    }

    *appstate = app;
    return SDL_APP_CONTINUE;
}

/// @brief Forward SDL events to the App instance.
/// @param appstate The App object.
/// @param event    The incoming SDL event.
/// @return Application continuation result.
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    return static_cast<App*>(appstate)->event(event);
}

/// @brief Run one iteration of the client app loop.
/// @param appstate The App object.
/// @return Application continuation result.
SDL_AppResult SDL_AppIterate(void* appstate)
{
    return static_cast<App*>(appstate)->iterate();
}

/// @brief Clean up and delete the App instance on exit.
/// @param appstate The App object.
/// @param result   Unused exit result code.
void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* app = static_cast<App*>(appstate);
    if (!app)
        return;
    app->quit();
    delete app;
}
