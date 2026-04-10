#pragma once

#include "Camera.hpp"
#include "ModelLoader.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <string>
#include <vector>

/// @brief SDL3 GPU renderer — forward PBR pipeline with HDR + tone mapping.
///
/// Render-pass architecture:
///   Pass 0 — Shadow map (depth-only from directional light, cascaded)
///   Pass 1 — Main colour pass (forward PBR into HDR render target)
///             ├ Skybox (procedural gradient / cubemap)
///             ├ Scene geometry (hard-coded cube + floor)
///             └ Loaded model (Assimp GLB meshes)
///   Pass 2 — Tone mapping (HDR → LDR swapchain) + ImGui overlay
///
/// Also owns the `imgui_impl_sdlgpu3` render backend.  The ImGui context and
/// SDL3 input backend are owned by DebugUI — initialise DebugUI first.
class Renderer
{
public:
    bool init(SDL_Window* window);
    void drawFrame(glm::vec3 eye, float yaw, float pitch);
    void requestScreenshot(const std::string& path);
    bool setVSync(bool enabled);
    void quit();

private:
    // ── Core GPU state ──────────────────────────────────────────────────────
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUTextureFormat swapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUShaderFormat shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;

    float fovyDegrees = 60.0f;
    float nearPlane = 5.0f;
    float farPlane = 15000.0f;
    Camera camera;

    // ── Pipelines ───────────────────────────────────────────────────────────
    SDL_GPUGraphicsPipeline* scenePipeline = nullptr;          ///< Hard-coded cube + floor (PBR lit).
    SDL_GPUGraphicsPipeline* pbrPipeline = nullptr;            ///< Assimp model — opaque meshes.
    SDL_GPUGraphicsPipeline* pbrTransparentPipeline = nullptr; ///< Same PBR — alpha-blended meshes.
    SDL_GPUGraphicsPipeline* skyboxPipeline = nullptr;         ///< Procedural/cubemap skybox.
    SDL_GPUGraphicsPipeline* tonemapPipeline = nullptr;        ///< Fullscreen HDR → LDR.
    SDL_GPUGraphicsPipeline* shadowPipeline = nullptr;         ///< Depth-only shadow map.

    // ── Render targets ──────────────────────────────────────────────────────
    SDL_GPUTexture* depthTexture = nullptr; ///< Scene depth, D32_FLOAT.
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;

    SDL_GPUTexture* hdrTarget = nullptr; ///< Main colour target, RGBA16F.
    Uint32 hdrWidth = 0;
    Uint32 hdrHeight = 0;

    static constexpr int k_shadowCascades = 4;
    static constexpr int k_shadowMapSize = 2048;
    SDL_GPUTexture* shadowMap = nullptr; ///< D32_FLOAT, 2D_ARRAY, 4 cascades.

    // ── IBL textures (Phase 6) ──────────────────────────────────────────────
    SDL_GPUTexture* brdfLUT = nullptr;       ///< 512×512 RG16F split-sum LUT.
    SDL_GPUTexture* irradianceMap = nullptr; ///< 32×32 per face, RGBA16F cubemap.
    SDL_GPUTexture* prefilterMap = nullptr;  ///< 128×128 per face, 5 mip levels, RGBA16F cubemap.
    SDL_GPUSampler* iblSampler = nullptr;    ///< Linear, clamp-to-edge, mipmapped.

    // ── Samplers ────────────────────────────────────────────────────────────
    SDL_GPUSampler* pbrSampler = nullptr;     ///< Linear, repeat, aniso 8×, mipmapped.
    SDL_GPUSampler* shadowSampler = nullptr;  ///< Comparison, border.
    SDL_GPUSampler* tonemapSampler = nullptr; ///< Linear, clamp-to-edge (for fullscreen pass).

    // ── Model rendering ─────────────────────────────────────────────────────
    struct GpuMesh
    {
        SDL_GPUBuffer* vertexBuffer = nullptr;
        SDL_GPUBuffer* indexBuffer = nullptr;
        Uint32 indexCount = 0;
        int albedoTexIndex = -1;
        int normalTexIndex = -1;
        int metallicRoughnessTexIndex = -1;
        int emissiveTexIndex = -1;
        MaterialData material;
        bool isTransparent = false; ///< True when alphaMode is BLEND or MASK.
    };

    /// One loaded model instance in the scene.
    struct ModelInstance
    {
        std::vector<GpuMesh> meshes;
        std::vector<SDL_GPUTexture*> textures;
        glm::mat4 transform{1.0f};
    };

    std::vector<ModelInstance> models;

    // Fallback 1×1 textures for missing PBR maps.
    SDL_GPUTexture* fallbackWhite = nullptr;      ///< Albedo / AO default.
    SDL_GPUTexture* fallbackFlatNormal = nullptr; ///< (0.5, 0.5, 1.0, 1.0).
    SDL_GPUTexture* fallbackMR = nullptr;         ///< (1.0, 0.5, 0, 0) = metallic=1, roughness=0.5.
    SDL_GPUTexture* fallbackBlack = nullptr;      ///< Emissive default.

    // ── Screen capture ──────────────────────────────────────────────────────
    SDL_GPUTexture* captureRT = nullptr;
    Uint32 captureRTW = 0, captureRTH = 0;
    SDL_GPUTextureFormat captureRTFmt = SDL_GPU_TEXTUREFORMAT_INVALID;
    std::string pendingCapPath;

    // ── Private helpers ─────────────────────────────────────────────────────
    bool initScenePipeline();
    bool initPBRPipeline();
    bool initSkyboxPipeline();
    bool initTonemapPipeline();
    bool initShadowPipeline();

    bool initIBL(); ///< Generate BRDF LUT, irradiance map, and prefilter map via compute.
    bool ensureDepthTexture(Uint32 w, Uint32 h);
    bool ensureHDRTarget(Uint32 w, Uint32 h);
    bool ensureCaptureRT(Uint32 w, Uint32 h, SDL_GPUTextureFormat fmt);

    bool uploadModel(const LoadedModel& model, ModelInstance& outInstance);
    SDL_GPUTexture* uploadTexture(const uint8_t* pixels, int width, int height, bool sRGB = true);

    void downloadAndSaveCapture(Uint32 w, Uint32 h);

    /// @brief Helper: load a compiled shader from the shaders/ directory.
    SDL_GPUShader* loadShaderFromFile(const char* name,
                                      SDL_GPUShaderStage stage,
                                      Uint32 samplerCount,
                                      Uint32 uniformBufferCount,
                                      Uint32 storageBufferCount = 0,
                                      Uint32 storageTextureCount = 0);
};
