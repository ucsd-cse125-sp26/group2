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
    // Create the ImGui context and initialise the SDL3 input backend.
    bool init(SDL_Window* window);

    // Shutdown SDL3 backend and destroy the ImGui context.
    void shutdown();

    // Forward one SDL event to ImGui. Call at the top of Game::event().
    void processEvent(const SDL_Event* event);

    // Start a new ImGui frame. Call at the top of Game::iterate().
    void newFrame();

    // Build the ECS inspector window for this frame.
    // tickCount is shown as a live counter so physics rate is visible.
    void buildUI(const Registry& registry, int tickCount);

    // Finalise ImGui rendering (calls ImGui::Render).
    // Must be called after buildUI() and before Renderer::drawFrame().
    void render();

private:
    // Per-component visibility toggles — persistent across frames.
    bool showPosition = true;
    bool showPrevPosition = false; // off by default — rarely useful at a glance
    bool showVelocity = true;
    bool showCollisionShape = true;
    bool showPlayerState = true;
    bool showInputSnapshot = true;
};
