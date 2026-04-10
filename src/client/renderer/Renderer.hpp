#pragma once

#include "Camera.hpp"
#include "ModelLoader.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <string>
#include <vector>

/// @brief SDL3 GPU pipeline (Vulkan · Metal · DX12).
///
/// Renders two layers of geometry each frame:
///   1. Scene geometry — hard-coded cube + floor via `projective.vert` / `normal.frag`.
///   2. Loaded model   — Assimp-imported GLB with per-mesh base-colour textures,
///                       rendered via `model.vert` / `model.frag`.
///
/// Also owns the `imgui_impl_sdlgpu3` render backend.  The ImGui context and
/// SDL3 input backend are owned by DebugUI — initialise DebugUI first, shut it down last.
class Renderer
{
public:
    /// @brief Initialise the GPU device, both pipelines, upload model + textures, init ImGui GPU.
    /// @param window  The SDL window to render into.
    /// @return False on any fatal GPU error.
    /// @pre An ImGui context must already exist (created by DebugUI::init).
    bool init(SDL_Window* window);

    /// @brief Request a PNG screenshot of the next rendered frame.
    /// The screenshot is saved synchronously at the end of that drawFrame() call,
    /// so it reflects exactly the pixels that appear on screen.
    /// @param path  Absolute path for the output PNG (parent directory must exist).
    void requestScreenshot(const std::string& path);

    /// @brief Submit the scene geometry, loaded model, and ImGui draw data for one frame.
    /// @param eye    World-space camera eye position (interpolated, in Quake units).
    /// @param yaw    Horizontal look angle in radians (matches InputSnapshot::yaw).
    /// @param pitch  Vertical look angle in radians (positive = looking down).
    void drawFrame(glm::vec3 eye, float yaw, float pitch);

    /// @brief Switch VSync on or off by changing the swapchain present mode.
    /// When disabled, prefers mailbox (no tearing) and falls back to immediate.
    /// @return False if the requested mode is unsupported on this device/platform.
    bool setVSync(bool enabled);

    /// @brief Release all GPU resources.  Waits for GPU idle before freeing.
    /// @pre Call before the SDL window is destroyed.
    void quit();

private:
    SDL_Window* window = nullptr;                ///< The SDL window being rendered into.
    SDL_GPUDevice* device = nullptr;             ///< The SDL GPU device.
    SDL_GPUGraphicsPipeline* pipeline = nullptr; ///< Scene pipeline (hard-coded geometry).

    float fovyDegrees = 60.0f;
    float nearPlane = 5.0f;                 ///< Near clip (Quake units); 5 ≈ half a foot.
    float farPlane = 15000.0f;              ///< Far clip; covers the 4 000-unit play area with margin.

    Camera camera;                          ///< First-person camera driven by player position + yaw/pitch each frame.

    SDL_GPUTexture* depthTexture = nullptr; ///< Depth buffer, recreated on resize.
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;

    // ---- Assimp model rendering ----------------------------------------

    /// @brief Per-mesh GPU resources for the loaded model.
    struct GpuMesh
    {
        SDL_GPUBuffer* vertexBuffer = nullptr;
        SDL_GPUBuffer* indexBuffer = nullptr;
        Uint32 indexCount = 0;
        int textureIndex = -1; ///< Index into modelTextures, or -1 → defaultTexture.
    };

    SDL_GPUGraphicsPipeline* modelPipeline = nullptr; ///< Pipeline with vertex-input layout + sampler.
    std::vector<GpuMesh> modelMeshes;                 ///< One entry per mesh in the loaded file.
    std::vector<SDL_GPUTexture*> modelTextures;       ///< Decoded textures uploaded to GPU.
    SDL_GPUTexture* defaultTexture = nullptr;         ///< 1×1 opaque-white fallback texture.
    SDL_GPUSampler* modelSampler = nullptr;           ///< Shared linear sampler for all model textures.
    glm::mat4 modelTransform{1.0f};                   ///< World transform applied to the model.

    // ---- Screen capture (recording) ------------------------------------

    SDL_GPUTexture* captureRT = nullptr; ///< Intermediate render target for screenshots.
    Uint32 captureRTW = 0;
    Uint32 captureRTH = 0;
    SDL_GPUTextureFormat captureRTFmt = SDL_GPU_TEXTUREFORMAT_INVALID; ///< Format mirrors swapchain.
    std::string pendingCapPath;                                        ///< Non-empty = save next frame here.

    // ---- Private helpers -----------------------------------------------

    /// @brief (Re-)create the depth texture when the swapchain size changes.
    bool ensureDepthTexture(Uint32 w, Uint32 h);

    /// @brief (Re-)create the intermediate capture render-target if size/format changed.
    bool ensureCaptureRT(Uint32 w, Uint32 h, SDL_GPUTextureFormat fmt);

    /// @brief Download captureRT to CPU and write a PNG to pendingCapPath, then clear it.
    void downloadAndSaveCapture(Uint32 w, Uint32 h);

    /// @brief Create the model graphics pipeline (vertex inputs + fragment sampler + depth test).
    /// @param fmt       Active shader format (SPIR-V or MSL).
    /// @param colorFmt  Swapchain colour target format.
    bool initModelPipeline(SDL_GPUShaderFormat fmt, SDL_GPUTextureFormat colorFmt);

    /// @brief Upload all mesh vertex/index data and textures to the GPU.
    /// Populates @c modelMeshes, @c modelTextures, @c defaultTexture, and @c modelSampler.
    bool uploadModel(const LoadedModel& model);

    /// @brief Upload one RGBA texture to a new GPU texture object.
    /// @return The created GPU texture, or nullptr on failure.
    SDL_GPUTexture* uploadTexture(const uint8_t* pixels, int width, int height);
};
