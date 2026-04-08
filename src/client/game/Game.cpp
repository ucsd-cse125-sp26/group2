#include "Game.hpp"

#include <SDL3/SDL_video.h>

#include <SDL3_net/SDL_net.h>

bool Game::init()
{
    SDL_SetAppMetadata("group2", "0.1.0", "com.cse125.group2");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!NET_Init()) {
        SDL_Log("NET_Init() failed: %s", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow("group2", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    if (!renderer.init(window)) {
        SDL_Log("Renderer init failed");
        SDL_DestroyWindow(window);
        return false;
    }

    if (!client.init("127.0.0.1", 9999)) {
        SDL_Log("Failed to connect to server");
        SDL_DestroyWindow(window);
        return false;
    }

    return true;
}

SDL_AppResult Game::event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_SPACE) {
        const char* msg = "Hello from client!";
        client.send(msg, static_cast<int>(strlen(msg)));
        SDL_Log("Sent message to server");
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult Game::iterate()
{
    while (client.poll()) {
    }
    renderer.drawFrame();
    return SDL_APP_CONTINUE;
}

void Game::quit()
{
    renderer.quit();
    client.shutdown();
    SDL_DestroyWindow(window);
    NET_Quit();
    SDL_Quit();
}
