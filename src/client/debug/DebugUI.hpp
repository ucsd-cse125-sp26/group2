#pragma once

#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// DebugUI — live ECS inspector overlay powered by Dear ImGui.
//
// Ownership split:
//   DebugUI  — ImGui context, imgui_impl_sdl3 input backend, UI state
//   Renderer — imgui_impl_sdlgpu3 render backend (owns the GPU device)
//
// Initialisation order in Game::init():
//   1. debugUI.init(window)   ← context must exist before GPU backend init
//   2. renderer.init(window)  ← GPU backend init happens here
//
// Shutdown order in Game::quit():
//   1. renderer.quit()        ← GPU backend shutdown first
//   2. debugUI.shutdown()     ← context destroyed last
// ---------------------------------------------------------------------------
class DebugUI
{
public:
    bool init(SDL_Window* window);
    void shutdown();
    void processEvent(const SDL_Event* event);
    void newFrame();
    void buildUI(const Registry& registry, int tickCount);
    void render();

private:
    // Per-component visibility toggles — persistent across frames.
    bool showPosition = true;
    bool showPrevPosition = false;
    bool showVelocity = true;
    bool showCollisionShape = true;
    bool showPlayerState = true;
    bool showInputSnapshot = true;
    bool showViewAngles = true; // yaw/pitch displayed in degrees for readability
};
