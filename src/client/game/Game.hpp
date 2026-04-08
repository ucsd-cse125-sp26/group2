#pragma once

#include "debug/DebugUI.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"
#include "renderer/Renderer.hpp"

#include <SDL3/SDL.h>

class Game
{
public:
    bool init();
    SDL_AppResult event(SDL_Event* event);
    SDL_AppResult iterate();
    void quit();

private:
    // Physics runs at a fixed step regardless of render frame rate.
    // The renderer interpolates between the previous and current physics
    // states using the leftover accumulator time as an alpha value.
    static constexpr int k_physicsHz = 128;
    static constexpr float k_physicsDt = 1.0f / static_cast<float>(k_physicsHz);

    SDL_Window* window = nullptr;
    DebugUI debugUI;
    Renderer renderer;
    Registry registry;
    Client client;

    Uint64 prevTime = 0;       // SDL performance counter at last iterate() call
    float accumulator = 0.0f;  // seconds of unprocessed physics time
    int tickCount = 0;         // total physics ticks since start
    bool mouseCaptured = true; // whether relative mouse mode is active
};
