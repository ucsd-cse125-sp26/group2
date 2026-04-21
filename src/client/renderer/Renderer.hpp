/// @file Renderer.hpp
/// @brief SDL3 GPU forward PBR renderer with HDR pipeline and post-processing.

#pragma once

#include "Camera.hpp"
#include "IRenderer.hpp"
#include "ModelLoader.hpp"
#include "RendererTypes.hpp"

#include <SDL3/SDL.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

class ParticleSystem; ///< Forward-declared to avoid circular includes.

/// @brief SDL3 GPU renderer -- forward PBR pipeline with HDR + tone mapping.
///
/// Render-pass architecture:
///   Pass 0 -- Shadow map (depth-only from directional light, cascaded)
///   Pass 1 -- Main colour pass (forward PBR into HDR render target)
///             - Skybox (procedural gradient / cubemap)
///             - Scene geometry (hard-coded cube + floor)
///             - Loaded model (Assimp GLB meshes)
///   Pass 2 -- Tone mapping (HDR -> LDR swapchain) + ImGui overlay
///
/// Also owns the imgui_impl_sdlgpu3 render backend.  The ImGui context and
/// SDL3 input backend are owned by DebugUI -- initialise DebugUI first.

/// @brief SDL3 GPU renderer with forward PBR pipeline.
class Renderer : public IRenderer
{
public:
    /// @brief Report which `RendererFeature` entries this renderer implements.
    ///        The legacy renderer implements all of them.
    [[nodiscard]] bool supports(RendererFeature /*feature*/) const override { return true; }

    /// @brief Initialise the GPU device, pipelines, and default scene assets.
    /// @param window SDL window to render into.
    /// @return True on success.
    bool init(SDL_Window* window) override;

    /// @brief Render one frame from the given camera pose.
    /// @param eye Camera world position.
    /// @param yaw Horizontal rotation in radians.
    /// @param pitch Vertical rotation in radians.
    /// @param roll Camera roll in radians (default 0).
    void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll) override;

    /// @brief Queue a screenshot to be saved after the next frame.
    /// @param path Output file path (PNG).
    void requestScreenshot(const std::string& path) override;

    /// @brief Enable or disable vertical sync.
    /// @param enabled True for VSync, false for uncapped.
    /// @return True on success.
    bool setVSync(bool enabled) override;

    /// @brief Release all GPU resources and shut down the renderer.
    void quit() override;

    // Particle system integration
    /// @brief Register a particle system to be rendered each frame (after scene, before ImGui).
    void setParticleSystem(ParticleSystem* ps) override { particleSystem = ps; }

    /// @brief Returns the SDL GPU device. Valid between init() and quit().
    [[nodiscard]] SDL_GPUDevice* getDevice() const override { return device; }

    /// @brief Returns the current camera (updated every drawFrame call).
    [[nodiscard]] const Camera& getCamera() const override { return camera; }

    /// @brief Shader format selected during init() (SPIR-V or MSL).
    [[nodiscard]] SDL_GPUShaderFormat getShaderFormat() const override { return shaderFormat; }

    /// @brief HDR render target format (RGBA16F). Particle pipelines must match this.
    [[nodiscard]] static constexpr SDL_GPUTextureFormat getHdrFormat()
    {
        return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    }

    // Entity rendering
    /// @brief Set the list of entity render commands for this frame.
    void setEntityRenderList(std::vector<EntityRenderCmd> cmds) override { entityRenderCmds = std::move(cmds); }

    /// @brief Set the first-person weapon viewmodel for this frame.
    void setWeaponViewmodel(const WeaponViewmodel& vm) override { weaponVM = vm; }

    /// @brief Load a model and return its index in the models[] vector, or -1 on failure.
    int loadSceneModel(const char* filename, glm::vec3 pos, float scale, bool flipUVs = false) override;

    /// @brief Upload a pre-built LoadedModel (e.g. from CharacterRig::templateLoadedModel()) and return its index.
    int uploadSceneModel(const LoadedModel& model) override;

    /// @brief Queue a skinned vertex re-upload for one mesh of an animated model.
    ///
    /// The actual GPU copy is deferred to the next drawFrame() command buffer --
    /// no separate command submission, no pipeline stall.
    /// @param modelIndex Index into the models[] vector.
    /// @param meshIndex Index into the model's meshes[] vector.
    /// @param vertices New vertex data.
    /// @param vertexCount Number of vertices.
    void
    updateModelMeshVertices(int modelIndex, int meshIndex, const ModelVertex* vertices, Uint32 vertexCount) override;

    /// @brief Returns the number of loaded models.
    [[nodiscard]] int modelCount() const override { return static_cast<int>(models.size()); }

    // HDR skybox
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
    // Core GPU state
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUTextureFormat swapchainFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUShaderFormat shaderFormat = SDL_GPU_SHADERFORMAT_INVALID;

    Camera camera;

    // Pipelines
    SDL_GPUGraphicsPipeline* scenePipeline = nullptr;          ///< Hard-coded cube + floor (PBR lit).
    SDL_GPUGraphicsPipeline* pbrPipeline = nullptr;            ///< Assimp model — opaque meshes.
    SDL_GPUGraphicsPipeline* pbrTransparentPipeline = nullptr; ///< Same PBR — alpha-blended meshes.
    SDL_GPUGraphicsPipeline* skyboxPipeline = nullptr;         ///< Procedural/cubemap skybox.
    SDL_GPUGraphicsPipeline* tonemapPipeline = nullptr;        ///< Fullscreen HDR → LDR.
    SDL_GPUGraphicsPipeline* shadowPipeline = nullptr;         ///< Depth-only shadow map.
    SDL_GPUGraphicsPipeline* sceneShadowPipeline = nullptr;    ///< Scene geometry into shadow map.

    // Render targets
    SDL_GPUTexture* depthTexture = nullptr;       ///< Scene depth, D32_FLOAT.
    SDL_GPUTexture* weaponDepthTexture = nullptr; ///< Weapon-only depth (keeps scene depth untouched).
    Uint32 depthWidth = 0;
    Uint32 depthHeight = 0;

    SDL_GPUTexture* hdrTarget = nullptr; ///< Main colour target, RGBA16F.
    Uint32 hdrWidth = 0;
    Uint32 hdrHeight = 0;

    static constexpr int k_shadowCascades = 4;
    static constexpr int k_shadowMapSize = 2048;
    SDL_GPUTexture* shadowMap = nullptr; ///< D32_FLOAT, 2D atlas (2×k_shadowMapSize)², 4 cascade quadrants.

    // IBL textures (Phase 6)
    SDL_GPUTexture* brdfLUT = nullptr;       ///< 512×512 RG16F split-sum LUT.
    SDL_GPUTexture* irradianceMap = nullptr; ///< 32×32 per face, RGBA16F cubemap for shader sampling.
    SDL_GPUTexture* prefilterMap = nullptr;  ///< 128×128 per face, 5 mip levels, RGBA16F cubemap for shader sampling.
    SDL_GPUTexture* irradianceWorkMap = nullptr; ///< 32×32 per face, RGBA16F 2D-array compute target.
    SDL_GPUTexture* prefilterWorkMap = nullptr;  ///< 128×128 per face, 5 mip levels, RGBA16F 2D-array compute target.
    SDL_GPUSampler* iblSampler = nullptr;        ///< Linear, clamp-to-edge, mipmapped.
    SDL_GPUTexture* envCubemap = nullptr;        ///< HDR environment cubemap (512×512, RGBA16F).

    // IBL compute pipelines (Phase 6) -- run once at startup, plus per HDR swap.
    SDL_GPUComputePipeline* brdfLutPipeline = nullptr;    ///< brdf_lut.comp
    SDL_GPUComputePipeline* irradiancePipeline = nullptr; ///< irradiance.comp
    SDL_GPUComputePipeline* prefilterPipeline = nullptr;  ///< prefilter.comp (per-mip dispatch)

    // Samplers
    SDL_GPUSampler* pbrSampler = nullptr;          ///< Linear, repeat, aniso 8×, mipmapped.
    SDL_GPUSampler* shadowSampler = nullptr;       ///< Comparison, border.
    SDL_GPUSampler* tonemapSampler = nullptr;      ///< Linear, clamp-to-edge (for fullscreen pass).
    SDL_GPUSampler* nearestDepthSampler = nullptr; ///< Nearest, clamp-to-edge (for depth in GTAO).

    // Model rendering

    /// @brief GPU-side mesh data for one mesh within a loaded model.
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

    /// @brief One loaded model instance in the scene.
    struct ModelInstance
    {
        std::vector<GpuMesh> meshes;
        std::vector<SDL_GPUTexture*> textures;
        glm::mat4 transform{1.0f};
        bool drawInScenePass = true; ///< False for models only used via EntityRenderCmd / WeaponViewmodel.
    };

    std::vector<ModelInstance> models;

    // Fallback 1x1 textures for missing PBR maps.
    SDL_GPUTexture* fallbackWhite = nullptr;      ///< Albedo / AO default.
    SDL_GPUTexture* fallbackFlatNormal = nullptr; ///< (0.5, 0.5, 1.0, 1.0).
    SDL_GPUTexture* fallbackMR = nullptr;         ///< glTF packed MR: metallic=0 (B=0), roughness=0.5 (G=128).
    SDL_GPUTexture* fallbackBlack = nullptr;      ///< Emissive default.

    // Particle system
    ParticleSystem* particleSystem = nullptr; ///< Optional; renders after scene geometry.

    // Entity rendering
    std::vector<EntityRenderCmd> entityRenderCmds; ///< Per-frame list from Game.
    WeaponViewmodel weaponVM;                      ///< First-person weapon, rendered after depth clear.

    // Deferred vertex re-uploads (skinned animation)
    // Queued by updateModelMeshVertices(), flushed at the start of drawFrame()
    // inside the main command buffer -- zero extra submits, zero pipeline stalls.

    /// @brief Queued vertex buffer update for skinned animation.
    struct PendingVertexUpload
    {
        SDL_GPUBuffer* dstBuffer = nullptr;
        std::vector<uint8_t> data;
    };
    std::vector<PendingVertexUpload> pendingVertexUploads;

    SDL_GPUTransferBuffer* skinTransferBuf = nullptr; ///< Persistent staging buffer (reused with cycle=true).
    Uint32 skinTransferBufSize = 0;                   ///< Current capacity in bytes.

    // Screen capture
    SDL_GPUTexture* captureRT = nullptr;
    Uint32 captureRTW = 0, captureRTH = 0;
    SDL_GPUTextureFormat captureRTFmt = SDL_GPU_TEXTUREFORMAT_INVALID;
    std::string pendingCapPath;

    // Post-processing (Phases 7-12)
    Uint32 postProcW = 0, postProcH = 0; ///< Screen dims used for post-processing texture allocation.

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

    // SMAA + Temporal Resolve (replaces old TAA)
    SDL_GPUGraphicsPipeline* smaaEdgePipeline = nullptr;  ///< Edge detection (smaa_fullscreen.vert + smaa_edge.frag).
    SDL_GPUGraphicsPipeline* smaaBlendPipeline = nullptr; ///< Blend weights (smaa_fullscreen.vert + smaa_blend.frag).
    SDL_GPUGraphicsPipeline* smaaNeighborhoodPipeline =
        nullptr; ///< Neighborhood blend (smaa_fullscreen.vert + smaa_neighborhood.frag).
    SDL_GPUComputePipeline* smaaResolvePipeline = nullptr; ///< Temporal resolve for T2x (smaa_resolve.comp).
    SDL_GPUTexture* smaaEdgeTex = nullptr;                 ///< RG8, screen-res.
    SDL_GPUTexture* smaaBlendTex = nullptr;                ///< RGBA8, screen-res.
    SDL_GPUTexture* smaaOutputTex = nullptr;               ///< RGBA16F, screen-res.
    SDL_GPUTexture* smaaAreaTex = nullptr;                 ///< RG8, 160x560 (static lookup).
    SDL_GPUTexture* smaaSearchTex = nullptr;               ///< R8, 66x33 (static lookup).

    // CAS (Contrast Adaptive Sharpening)
    SDL_GPUComputePipeline* casPipeline = nullptr; ///< cas.comp.
    SDL_GPUTexture* casOutputTex = nullptr;        ///< RGBA16F, screen-res.

    // Temporal history + motion vectors (reused from old TAA)
    SDL_GPUTexture* taaHistory[2] = {};            ///< Ping-pong RGBA16F for temporal resolve.
    int taaCurrentIdx = 0;
    glm::mat4 previousVP{1.0f};                    ///< Previous frame's view-projection for motion vectors.
    SDL_GPUTexture* motionVectorTexture = nullptr; ///< RG16F screen-res.
    SDL_GPUComputePipeline* motionVectorPipeline = nullptr;

    // Jitter
    int frameIndex = 0; ///< Alternates 0/1 for T2x temporal jitter.

    // SSR (Phase 9) — ping-pong for temporal accumulation.
    SDL_GPUTexture* ssrTexture[2] = {}; ///< RGBA16F, ping-pong.
    int ssrCurrentIdx = 0;
    SDL_GPUComputePipeline* ssrPipeline = nullptr;

public:
    int ssrMode = 2;       ///< 0=Sharp, 1=Stochastic, 2=Masked (default).
    RenderToggles toggles; ///< Live-tunable feature toggles (checked every frame).

    // Anti-aliasing (live-tunable via ImGui)
    AAMode aaMode = AAMode::SMAA_T2x; ///< Current AA mode (default: recommended T2x).
    bool casEnabled = true;           ///< CAS sharpening on/off.
    float casStrength = 1.0f;         ///< CAS sharpness (0.0 = minimal, 1.0 = max).

    // Sun / lighting (live-tunable via ImGui)
    float sunAzimuth = 210.0f;  ///< Degrees, 0=North, 90=East, 180=South (default ~SSW).
    float sunElevation = 60.0f; ///< Degrees above horizon (default 60° ≈ 11am).
    float sunIntensity = 3.0f;  ///< Primary directional light intensity.
    float fillIntensity = 0.8f; ///< Fill/bounce light intensity.
    float ambientR = 0.08f, ambientG = 0.09f, ambientB = 0.12f; ///< PBR ambient color.
    float iblDiffuseIntensity = 1.0f;                           ///< Multiplier on IBL diffuse term.
    /// Multiplier on IBL specular term -- default below 1.0 to tame the
    /// over-glossy / pseudo-metallic look dielectric surfaces get from
    /// real-world HDR environment maps.
    float iblSpecularIntensity = 0.5f;
    float bloomStr = 0.04f;   ///< Bloom compositing strength.
    float ssaoStr = 0.8f;     ///< SSAO compositing strength.
    float ssaoRadius = 0.8f;  ///< GTAO world-space radius.
    float ssaoFalloff = 2.0f; ///< GTAO distance falloff exponent.
    float ssaoPower = 1.5f;   ///< AO power curve (1=linear, higher=softer).
    float ssrStr = 0.4f;      ///< SSR compositing strength.
    float volStr = 0.15f;     ///< Volumetric compositing strength.
    float sharpenStr = 0.6f;  ///< Post-TAA sharpening strength.
    float shadowBiasVal = 0.0005f;
    float shadowNormalBiasVal = 1.5f;
    float shadowDistance = 3000.0f; ///< Max shadow range (world units).
    float cascadeLambda = 0.92f;    ///< Log vs linear cascade split blend (0=linear, 1=log).

    /// @brief Compute the sun direction (unit vector TO sun) from azimuth/elevation.
    /// @return Normalised direction vector pointing toward the sun.
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

    // Private helpers
    bool initScenePipeline();
    bool initPBRPipeline();
    bool initSkyboxPipeline();
    bool initTonemapPipeline();
    bool initShadowPipeline();
    bool initSceneShadowPipeline();

    bool initIBL();

    /// @brief Run the irradiance + prefilter compute passes against `envCube`.
    ///
    /// Used both at startup (after uploading the procedural sky into envCubemap)
    /// and after loading a new HDR file. Assumes irradianceMap and prefilterMap
    /// already exist with COMPUTE_STORAGE_WRITE usage.
    /// @param envCube  HDR environment cubemap (already uploaded).
    /// @return False if the dispatches could not be issued.
    bool regenerateIBLFromCubemap(SDL_GPUTexture* envCube);

    bool initBloom();
    bool initSSAO();
    bool initSMAA(); ///< Upload area/search textures, create SMAA + temporal resolve pipelines.
    bool initCAS();  ///< Create CAS compute pipeline.
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
    /// @brief Ensure the depth texture exists at the given resolution, recreating if needed.
    bool ensureDepthTexture(Uint32 w, Uint32 h);

    /// @brief Ensure the HDR render target exists at the given resolution.
    bool ensureHDRTarget(Uint32 w, Uint32 h);

    /// @brief Ensure the capture render target exists for screenshot download.
    bool ensureCaptureRT(Uint32 w, Uint32 h, SDL_GPUTextureFormat fmt);

    /// @brief Upload a LoadedModel to the GPU and populate a ModelInstance.
    bool uploadModel(const LoadedModel& model, ModelInstance& outInstance);

    /// @brief Upload RGBA pixel data and generate a mip chain.
    /// @param pixels Raw RGBA pixel data.
    /// @param width Texture width in pixels.
    /// @param height Texture height in pixels.
    /// @param sRGB True for sRGB colour textures, false for linear data.
    /// @return Created GPU texture, or nullptr on failure.
    SDL_GPUTexture* uploadTexture(const uint8_t* pixels, int width, int height, bool sRGB = true);

    /// @brief Download the capture render target to CPU and write a PNG file.
    void downloadAndSaveCapture(Uint32 w, Uint32 h);

    /// @brief Helper: load a compiled shader from the shaders/ directory.
    SDL_GPUShader* loadShaderFromFile(const char* name,
                                      SDL_GPUShaderStage stage,
                                      Uint32 samplerCount,
                                      Uint32 uniformBufferCount,
                                      Uint32 storageBufferCount = 0,
                                      Uint32 storageTextureCount = 0);
};
