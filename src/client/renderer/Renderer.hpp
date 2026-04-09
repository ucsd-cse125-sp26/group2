#pragma once

#include "Camera.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>

/// @brief SDL3 GPU pipeline (Vulkan · Metal · DX12).
///
/// Also owns the `imgui_impl_sdlgpu3` render backend. The ImGui context and
/// SDL3 input backend are owned by DebugUI — initialise DebugUI first, shut it down last.
///
/// Shaders: `shaders/projective.vert` + `shaders/normal.frag`
/// (compiled GLSL → SPIR-V at build time via glslc/glslangValidator).
class Renderer
{
public:
    /// @brief Initialise the GPU device, pipeline, and ImGui GPU backend.
    /// @param window  The SDL window to render into.
    /// @return False on any fatal GPU error.
    /// @pre An ImGui context must already exist (created by DebugUI::init).
    bool init(SDL_Window* window);

    /// @brief Submit the scene geometry and ImGui draw data for one frame.
    void drawFrame();

    /// @brief Release all GPU resources. Waits for GPU idle before freeing.
    /// @pre Call before the SDL window is destroyed.
    void quit();

    void rotateCameraRight(float degrees) { camera.rotateRight(degrees); }
    void rotateCameraUp(float degrees) { camera.rotateUp(degrees); }
    void resetCamera() { camera.reset(); }

private:
    SDL_Window* window = nullptr;                ///< The SDL window being rendered into.
    SDL_GPUDevice* device = nullptr;             ///< The SDL GPU device.
    SDL_GPUGraphicsPipeline* pipeline = nullptr; ///< The scene graphics pipeline.

    // Camera initial parameters (passed to Camera constructor in init()).
    glm::vec3 eye = glm::vec3(5.0f, 0.0f, 0.0f);
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    float fovy = glm::radians(60.0f); ///< Field of view in radians (converted to degrees for Camera).
    float near = 0.01f;
    float far = 100.0f;

    Camera camera;                          ///< Orbiting camera — rotate with W/A/S/D, reset with R.

    SDL_GPUTexture* depthTexture = nullptr; ///< Depth buffer, recreated on resize.
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;

    /// @brief (Re-)create the depth texture when the swapchain size changes.
    bool ensureDepthTexture(Uint32 w, Uint32 h);
};
