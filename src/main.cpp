#define SDL_MAIN_USE_CALLBACKS // For using callbacks instead of a main() entrypoint
#include "renderer/renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// ---------------------------------------------------------------------------
// SDL3 app callbacks
// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
    return rendererInit(appstate, 0, nullptr);
}

SDL_AppResult SDL_AppEvent(void* /*appstate*/, SDL_Event* event)
{
    return rendererAppEvent(nullptr, event);
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    return rendererAppIterate(appstate);
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    return rendererAppQuit(appstate, SDL_APP_SUCCESS);
}
