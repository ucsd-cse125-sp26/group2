#pragma once

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// Renderer — SDL3 GPU pipeline (Vulkan · Metal · DX12).
//
// Also owns the imgui_impl_sdlgpu3 render backend. The ImGui context and
// SDL3 input backend are owned by DebugUI, which must be initialised before
// this class and shut down after it.
//
// Shaders: shaders/triangle.vert + shaders/triangle.frag
//   Compiled from GLSL to SPIR-V at build time (glslc or glslangValidator).
// ---------------------------------------------------------------------------
class Renderer
{
public:
    // Called once after the window is created. Returns false on fatal error.
    // Assumes an ImGui context already exists (created by DebugUI::init).
    bool init(SDL_Window* window);

    // Called every frame. Submits the scene triangle then ImGui draw data.
    void drawFrame();

    // Called before the window is destroyed. Waits for GPU idle, frees all
    // resources including the imgui_impl_sdlgpu3 backend.
    void quit();

private:
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};
