#pragma once

#include "Camera.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <vector>

struct Matrices
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
};

struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texUV;
};

struct ModelBufferInfo
{
    void* srcData;
    SDL_GPUBuffer* gpuBuff;
    Uint32 bufferSize;
};

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
    SDL_Window* window_ = nullptr;                ///< The SDL window being rendered into.
    SDL_GPUDevice* device_ = nullptr;             ///< The SDL GPU device.
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr; ///< The scene graphics pipeline.

    // Camera parameters used during init. Near/far are sized for Quake units.
    float fovyDegrees_ = 60.0f;
    float nearPlane_ = 5.0f;                ///< Near clip (Quake units); 5 ≈ half a foot.
    float farPlane_ = 15000.0f;             ///< Far clip; covers the 4 000-unit play area with margin.

    Camera camera_;                         ///< First-person camera — driven by player position + yaw/pitch each frame.

    SDL_GPUTexture* depthTexture = nullptr; ///< Depth buffer, recreated on resize.
    Uint32 depthWidth_ = 0;
    Uint32 depthHeight_ = 0;

    ModelBufferInfo vBufferInfo_;
    ModelBufferInfo iBufferInfo_;

    /// @brief (Re-)create the depth texture when the swapchain size changes.
    bool ensureDepthTexture(Uint32 w, Uint32 h);

    [[nodiscard]] SDL_GPUTransferBuffer* createTransferBuffer(size_t transferBufferSize, bool upload) const;
    [[nodiscard]] SDL_GPUBuffer* createGPUBuffer(size_t bufferSize, SDL_GPUBufferUsageFlags usage) const;
    void uploadDataToGPUBuffer(SDL_GPUCommandBuffer* cmd, const std::vector<ModelBufferInfo>& modelBuffersInfo) const;
};
