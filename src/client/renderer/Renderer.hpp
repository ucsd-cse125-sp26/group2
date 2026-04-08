#pragma once

#include <SDL3/SDL.h>

// ---------------------------------------------------------------------------
// Renderer — SDL3 GPU pipeline (Vulkan · Metal · DX12).
//
// Shaders: shaders/triangle.vert + shaders/triangle.frag
//   Compiled from GLSL to SPIR-V at build time (glslc or glslangValidator).
//   SDL3 converts SPIR-V → MSL internally at runtime on macOS/Metal —
//   no spirv-cross build dependency needed.
// ---------------------------------------------------------------------------

class Renderer
{
public:
    // Called once after the window is created. Returns false on fatal error.
    bool init(SDL_Window* window);

    // Called every frame. Records and submits one frame to the GPU.
    void drawFrame();

    // Called before the window is destroyed. Waits for GPU idle, frees all resources.
    void quit();

private:
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
};
