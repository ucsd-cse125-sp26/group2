#ifndef GROUP2_RENDERER_H
#define GROUP2_RENDERER_H

SDL_AppResult rendererInit(void** appstate, int /*argc*/, char* /*argv*/[]);

SDL_AppResult rendererAppEvent(void* /*appstate*/, SDL_Event* event);

SDL_AppResult rendererAppIterate(void* appstate);

void rendererAppQuit(void* appstate, SDL_AppResult /*result*/);

#endif // GROUP2_RENDERER_H
