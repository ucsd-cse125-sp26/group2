#pragma once

#include "Camera.hpp"
#include "ModelLoader.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

class ParticleSystem; ///< Forward-declared to avoid circular includes.

/// @brief Live toggles for every render system — exposed to ImGui.
///
/// All default to true (everything on).  The Renderer checks these each frame
/// and skips the corresponding pass/dispatch when disabled.
struct RenderToggles
{
    // ── Geometry passes ─────────────────────────────────────────────────────
    bool sceneGeometry = true;   ///< Hard-coded cube + floor.
    bool pbrModels = true;       ///< Assimp-loaded scene models (opaque + transparent).
    bool entityModels = true;    ///< ECS-driven entity models (Renderable component).
    bool weaponViewmodel = true; ///< First-person weapon.
    bool skybox = true;          ///< Procedural / cubemap skybox.

    // ── Shadow ──────────────────────────────────────────────────────────────
    bool shadows = true; ///< Shadow map pass + shadow sampling in PBR.

    // ── Post-processing ─────────────────────────────────────────────────────
    bool ssao = true;        ///< Screen-space ambient occlusion.
    bool bloom = true;       ///< Bloom downsample + upsample chain.
    bool ssr = true;         ///< Screen-space reflections.
    bool volumetrics = true; ///< Volumetric lighting / god rays.
    bool taa = true;         ///< Temporal anti-aliasing.
    bool tonemap = true;     ///< HDR → LDR tone mapping (disabling = raw HDR blit).

    // ── Effects ─────────────────────────────────────────────────────────────
    bool particles = true; ///< GPU particle system.
    bool sdfText = true;   ///< SDF text rendering (HUD + world).
};

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
/// @brief Per-entity render command — built by Game, consumed by Renderer::drawFrame.
struct EntityRenderCmd
{
    int32_t modelIndex = -1;        ///< Index into Renderer::models[].
    glm::mat4 worldTransform{1.0f}; ///< Full world transform (position × rotation × scale).
};

/// @brief First-person weapon viewmodel descriptor.
struct WeaponViewmodel
{
    int32_t modelIndex = -1;   ///< Index into Renderer::models[].
    glm::mat4 transform{1.0f}; ///< Transform in viewmodel space (relative to camera).
    bool visible = false;
};

class Renderer
{
public:
    bool init(SDL_Window* window);
    void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll = 0.0f);
    void requestScreenshot(const std::string& path);
    bool setVSync(bool enabled);
    void quit();

    // ── Particle system integration ────────────────────────────────────────
    /// @brief Register a particle system to be rendered each frame (after scene, before ImGui).
    void setParticleSystem(ParticleSystem* ps) { particleSystem = ps; }

    /// @brief Returns the SDL GPU device. Valid between init() and quit().
    [[nodiscard]] SDL_GPUDevice* getDevice() const { return device; }

    /// @brief Returns the current camera (updated every drawFrame call).
    [[nodiscard]] const Camera& getCamera() const { return camera; }

    /// @brief Shader format selected during init() (SPIR-V or MSL).
    [[nodiscard]] SDL_GPUShaderFormat getShaderFormat() const { return shaderFormat; }

    /// @brief HDR render target format (RGBA16F). Particle pipelines must match this.
    [[nodiscard]] static constexpr SDL_GPUTextureFormat getHdrFormat()
    {
        return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    }

    // ── Entity rendering ───────────────────────────────────────────────────
    /// @brief Set the list of entity render commands for this frame.
    void setEntityRenderList(std::vector<EntityRenderCmd> cmds) { entityRenderCmds = std::move(cmds); }

    /// @brief Set the first-person weapon viewmodel for this frame.
    void setWeaponViewmodel(const WeaponViewmodel& vm) { weaponVM = vm; }

    /// @brief Load a model and return its index in the models[] vector, or -1 on failure.
    int loadSceneModel(const char* filename, glm::vec3 pos, float scale, bool flipUVs = false);

    /// @brief Upload a pre-built LoadedModel (e.g. from SkinnedModel) and return its index.
    int uploadSceneModel(const LoadedModel& model);

    /// @brief Queue a skinned vertex re-upload for one mesh of an animated model.
    /// The actual GPU copy is deferred to the next drawFrame() command buffer —
    /// no separate command submission, no pipeline stall.
    void updateModelMeshVertices(int modelIndex, int meshIndex, const ModelVertex* vertices, Uint32 vertexCount);

    /// @brief Returns the number of loaded models.
    [[nodiscard]] int modelCount() const { return static_cast<int>(models.size()); }

    // ── HDR skybox ────────────────────────────────────────────────────────
    /// @brief Load an equirectangular HDR image as the environment skybox + IBL source.
    bool loadHDRSkybox(const std::string& path);
    /// @brief Scan the assets HDR directory and populate availableHDRFiles.
    void scanHDRFiles();
    /// @brief Available HDR file paths (populated by scanHDRFiles).
    std::vector<std::string> availableHDRFiles;
    /// @brief Currently loaded HDR file (display name).
    std::string currentHDRName = "(procedural)";
    /// @brief True when an HDR cubemap is loaded and should be used for skybox + IBL.
    bool useHDRSkybox = false;

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
    SDL_GPUGraphicsPipeline* sceneShadowPipeline = nullptr;    ///< Scene geometry into shadow map.

    // ── Render targets ──────────────────────────────────────────────────────
    SDL_GPUTexture* depthTexture = nullptr; ///< Scene depth, D32_FLOAT.
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;

    SDL_GPUTexture* hdrTarget = nullptr; ///< Main colour target, RGBA16F.
    Uint32 hdrWidth = 0;
    Uint32 hdrHeight = 0;

    static constexpr int k_shadowCascades = 4;
    static constexpr int k_shadowMapSize = 2048;
    SDL_GPUTexture* shadowMap = nullptr; ///< D32_FLOAT, 2D atlas (2×k_shadowMapSize)², 4 cascade quadrants.

    // ── IBL textures (Phase 6) ──────────────────────────────────────────────
    SDL_GPUTexture* brdfLUT = nullptr;       ///< 512×512 RG16F split-sum LUT.
    SDL_GPUTexture* irradianceMap = nullptr; ///< 32×32 per face, RGBA16F cubemap.
    SDL_GPUTexture* prefilterMap = nullptr;  ///< 128×128 per face, 5 mip levels, RGBA16F cubemap.
    SDL_GPUSampler* iblSampler = nullptr;    ///< Linear, clamp-to-edge, mipmapped.
    SDL_GPUTexture* envCubemap = nullptr;    ///< HDR environment cubemap (512×512, RGBA16F).

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
        bool drawInScenePass = true; ///< False for models only used via EntityRenderCmd / WeaponViewmodel.
    };

    std::vector<ModelInstance> models;

    // Fallback 1×1 textures for missing PBR maps.
    SDL_GPUTexture* fallbackWhite = nullptr;      ///< Albedo / AO default.
    SDL_GPUTexture* fallbackFlatNormal = nullptr; ///< (0.5, 0.5, 1.0, 1.0).
    SDL_GPUTexture* fallbackMR = nullptr;         ///< (1.0, 0.5, 0, 0) = metallic=1, roughness=0.5.
    SDL_GPUTexture* fallbackBlack = nullptr;      ///< Emissive default.

    // ── Particle system ──────────────────────────────────────────────────────
    ParticleSystem* particleSystem = nullptr; ///< Optional; renders after scene geometry.

    // ── Entity rendering ────────────────────────────────────────────────────
    std::vector<EntityRenderCmd> entityRenderCmds; ///< Per-frame list from Game.
    WeaponViewmodel weaponVM;                      ///< First-person weapon, rendered after depth clear.

    // ── Deferred vertex re-uploads (skinned animation) ────────────────────
    // Queued by updateModelMeshVertices(), flushed at the start of drawFrame()
    // inside the main command buffer — zero extra submits, zero pipeline stalls.
    struct PendingVertexUpload
    {
        SDL_GPUBuffer* dstBuffer = nullptr;
        std::vector<uint8_t> data;
    };
    std::vector<PendingVertexUpload> pendingVertexUploads;

    SDL_GPUTransferBuffer* skinTransferBuf = nullptr; ///< Persistent staging buffer (reused with cycle=true).
    Uint32 skinTransferBufSize = 0;                   ///< Current capacity in bytes.

    // ── Screen capture ──────────────────────────────────────────────────────
    SDL_GPUTexture* captureRT = nullptr;
    Uint32 captureRTW = 0, captureRTH = 0;
    SDL_GPUTextureFormat captureRTFmt = SDL_GPU_TEXTUREFORMAT_INVALID;
    std::string pendingCapPath;

    // ── Post-processing (Phases 7-12) ─────────────────────────────────────
    // Bloom (Phase 8)
    static constexpr int k_bloomMips = 6;
    SDL_GPUTexture* bloomMips[k_bloomMips] = {}; ///< Downsample chain, RGBA16F.
    SDL_GPUComputePipeline* bloomDownsamplePipeline = nullptr;
    SDL_GPUComputePipeline* bloomUpsamplePipeline = nullptr;

    // SSAO (Phase 7)
    SDL_GPUTexture* ssaoTexture = nullptr;     ///< R8_UNORM, screen-res.
    SDL_GPUTexture* ssaoBlurTexture = nullptr; ///< R8_UNORM, blurred.
    SDL_GPUComputePipeline* ssaoPipeline = nullptr;
    SDL_GPUComputePipeline* ssaoBlurPipeline = nullptr;
    SDL_GPUTexture* ssaoNoiseTexture = nullptr; ///< 4×4 random rotations.

    // TAA (Phase 11)
    SDL_GPUTexture* taaHistory[2] = {};            ///< Ping-pong RGBA16F.
    int taaCurrentIdx = 0;
    glm::mat4 previousVP{1.0f};                    ///< Previous frame's view-projection for motion vectors.
    SDL_GPUComputePipeline* taaPipeline = nullptr;
    SDL_GPUTexture* motionVectorTexture = nullptr; ///< RG16F screen-res.
    SDL_GPUComputePipeline* motionVectorPipeline = nullptr;

    // SSR (Phase 9) — ping-pong for temporal accumulation.
    SDL_GPUTexture* ssrTexture[2] = {}; ///< RGBA16F, ping-pong.
    int ssrCurrentIdx = 0;
    SDL_GPUComputePipeline* ssrPipeline = nullptr;

public:
    int ssrMode = 2;       ///< 0=Sharp, 1=Stochastic, 2=Masked (default).
    RenderToggles toggles; ///< Live-tunable feature toggles (checked every frame).

    // ── Sun / lighting (live-tunable via ImGui) ────────────────────────────
    float sunAzimuth = 210.0f;  ///< Degrees, 0=North, 90=East, 180=South (default ~SSW).
    float sunElevation = 60.0f; ///< Degrees above horizon (default 60° ≈ 11am).
    float sunIntensity = 3.0f;  ///< Primary directional light intensity.
    float fillIntensity = 0.8f; ///< Fill/bounce light intensity.
    float ambientR = 0.08f, ambientG = 0.09f, ambientB = 0.12f; ///< PBR ambient color.
    float bloomStr = 0.04f;                                     ///< Bloom compositing strength.
    float ssaoStr = 0.8f;                                       ///< SSAO compositing strength.
    float ssrStr = 0.4f;                                        ///< SSR compositing strength.
    float volStr = 0.15f;                                       ///< Volumetric compositing strength.
    float sharpenStr = 0.6f;                                    ///< Post-TAA sharpening strength.
    float shadowBiasVal = 0.0005f;
    float shadowNormalBiasVal = 1.5f;
    float shadowDistance = 3000.0f; ///< Max shadow range (world units).
    float cascadeLambda = 0.92f;    ///< Log vs linear cascade split blend (0=linear, 1=log).

    /// Compute the sun direction (unit vector TO sun) from azimuth/elevation.
    [[nodiscard]] glm::vec3 getSunDirection() const;

private:
    // Volumetrics (Phase 10)
    SDL_GPUTexture* volumetricTexture = nullptr; ///< RGBA16F half-res.
    SDL_GPUComputePipeline* volumetricPipeline = nullptr;

    // OIT (Phase 12)
    SDL_GPUTexture* oitAccumTexture = nullptr;  ///< RGBA16F.
    SDL_GPUTexture* oitRevealTexture = nullptr; ///< R8_UNORM.
    SDL_GPUGraphicsPipeline* oitPipeline = nullptr;
    SDL_GPUGraphicsPipeline* oitResolvePipeline = nullptr;

    // ── Private helpers ─────────────────────────────────────────────────────
    bool initScenePipeline();
    bool initPBRPipeline();
    bool initSkyboxPipeline();
    bool initTonemapPipeline();
    bool initShadowPipeline();
    bool initSceneShadowPipeline();

    bool initIBL();
    bool initBloom();
    bool initSSAO();
    bool initTAA();
    bool initSSR();
    bool initVolumetrics();

    /// @brief Helper: create a compute pipeline from a compiled shader file.
    SDL_GPUComputePipeline* createComputePipeline(const char* shaderName,
                                                  Uint32 numSamplers,
                                                  Uint32 numReadonlyStorageTextures,
                                                  Uint32 numReadonlyStorageBuffers,
                                                  Uint32 numReadwriteStorageTextures,
                                                  Uint32 numReadwriteStorageBuffers,
                                                  Uint32 numUniformBuffers,
                                                  Uint32 threadCountX,
                                                  Uint32 threadCountY,
                                                  Uint32 threadCountZ);
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
