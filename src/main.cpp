#define SDL_MAIN_USE_CALLBACKS
#include "net/Client.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <SDL3_net/SDL_net.h>

// ---------------------------------------------------------------------------
// Select rendering backend at compile time.
//   cmake --preset debug -DUSE_OPENGL=ON   → OpenGL 4.1 core (glad)
//   cmake --preset debug                   → SDL3 GPU pipeline (default)
// ---------------------------------------------------------------------------

#ifdef USE_OPENGL
#include "renderer/OpenGLRenderer.hpp"
using ActiveRenderer = OpenGLRenderer;
#else
#include "renderer/SDLGPURenderer.hpp"
using ActiveRenderer = SDLGPURenderer;
#endif

#include "ecs/Registry.hpp"

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

struct AppState
{
    SDL_Window* window = nullptr;
    ActiveRenderer renderer;
    Registry registry;
    Client client;
};

// ---------------------------------------------------------------------------
// SDL3 app callbacks
// ---------------------------------------------------------------------------

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char* /*argv*/[])
{
    SDL_SetAppMetadata("group2", "0.1.0", "com.cse125.group2");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // OpenGL: set context attributes before the window is created.
#ifdef USE_OPENGL
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

    auto* s = new AppState();
    *appstate = s;

    constexpr int k_winW = 1280;
    constexpr int k_winH = 720;

#ifdef USE_OPENGL
    constexpr SDL_WindowFlags k_winFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#else
    constexpr SDL_WindowFlags k_winFlags = SDL_WINDOW_RESIZABLE;
#endif

    s->window = SDL_CreateWindow("group2", k_winW, k_winH, k_winFlags);
    if (!s->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!s->renderer.init(s->window)) {
        SDL_Log("Renderer init failed");
        return SDL_APP_FAILURE;
    }

#ifdef USE_OPENGL
    SDL_Log("Backend: OpenGL 4.1 core");
#else
    SDL_Log("Backend: SDL3 GPU pipeline");
#endif

    if (!NET_Init()) {
        SDL_Log("NET_Init() failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("SDL_net initialized successfully");

    NET_Address* addr = NET_ResolveHostname("127.0.0.1");
    while (NET_GetAddressStatus(addr) == NET_WAITING) {
        SDL_Delay(100);
    }
    if (NET_GetAddressStatus(addr) == NET_FAILURE) {
        SDL_Log("Failed to resolve address: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!s->client.init(addr, 9999)) {
        SDL_Log("Failed to connect to server");
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* s = static_cast<AppState*>(appstate);
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        return SDL_APP_SUCCESS;

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_SPACE) {
        const char* msg = "Hello from client!";
        s->client.send(msg, (int)strlen(msg));
        SDL_Log("Sent message to server");
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* s = static_cast<AppState*>(appstate);

    while (s->client.poll()) {
    }

    s->renderer.renderFrame();
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* s = static_cast<AppState*>(appstate);
    if (!s)
        return;
    s->renderer.shutdown();
    s->client.shutdown();
    SDL_DestroyWindow(s->window);
    NET_Quit();
    SDL_Quit();
    delete s;
}
