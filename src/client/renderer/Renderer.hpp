#pragma once

#include "Camera.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

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

    void rotateCameraRight(float degrees) { camera.rotateRight(degrees); }
    void rotateCameraUp(float degrees) { camera.rotateUp(degrees); }
    void resetCamera() { camera.reset(); }

private:
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;

    // Camera stuff
    glm::vec3 eye = glm::vec3(5.0f, 0.0f, 0.0f);
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    float fovy = glm::radians(60.0f);
    float near = 0.01f;
    float far = 100.0f;

    glm::mat4 view = glm::mat4(1.0f); // view matrix
    glm::mat4 proj = glm::mat4(1.0f); // projection matrix

    SDL_GPUTexture* depthTexture = nullptr;
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;

    bool ensureDepthTexture(Uint32 w, Uint32 h);

    Camera camera;
};
