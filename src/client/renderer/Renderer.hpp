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
    /// @param eye    World-space camera eye position (interpolated, in Quake units).
    /// @param yaw    Horizontal look angle in radians (matches InputSnapshot::yaw).
    /// @param pitch  Vertical look angle in radians (positive = looking down).
    void drawFrame(glm::vec3 eye, float yaw, float pitch);

    /// @brief Release all GPU resources. Waits for GPU idle before freeing.
    /// @pre Call before the SDL window is destroyed.
    void quit();

private:
    SDL_Window* window = nullptr;                ///< The SDL window being rendered into.
    SDL_GPUDevice* device = nullptr;             ///< The SDL GPU device.
    SDL_GPUGraphicsPipeline* pipeline = nullptr; ///< The scene graphics pipeline.

    // Camera parameters used during init. Near/far are sized for Quake units.
    float fovyDegrees = 60.0f;
    float nearPlane = 5.0f;    ///< Near clip (Quake units); 5 ≈ half a foot.
    float farPlane = 15000.0f; ///< Far clip; covers the 4 000-unit play area with margin.

    Camera camera;             ///< First-person camera — driven by player position + yaw/pitch each frame.

    SDL_GPUTexture* depthTexture = nullptr; ///< Depth buffer, recreated on resize.
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;

    /// @brief (Re-)create the depth texture when the swapchain size changes.
    bool ensureDepthTexture(Uint32 w, Uint32 h);
};
