/// @file NewRenderer.hpp
/// @brief Work-in-progress SDL3 GPU renderer.
///
/// API surface restored from the legacy renderer so gameplay code has a stable
/// contract to call against while graphics team fills in implementations.  Most
/// `set*()` methods are currently DATA-CAPTURE STUBS — they store inputs into
/// member fields and DO NOT yet drive the GPU.  Each stub's doc comment lists:
///   * what the inputs mean,
///   * what the implementation should produce on-screen,
///   * which member field holds the captured data,
///   * where the data comes from (call site).
///
/// Search the cpp for `TODO(graphics)` to find every stub awaiting work.

#pragma once

#include "Asset.hpp"
#include "Boilerplate.hpp"
#include "Camera.hpp"
#include "RendererTypes.hpp"
#include "SkinnedRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <queue>
#include <string>
#include <vector>

#define NUM_CUBE_FACES 6
class ParticleSystem; ///< Forward-declared — owned by Game, registered via setParticleSystem().

/// @brief Vertex attribute layout for the static geometry pipeline.
///
/// 48 bytes: position (12) + normal (12) + texUV (8) + tangent (16).
/// Layout matches `ModelVertex`'s static attributes but remains a distinct
/// type because static meshes do not carry skinning data.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texUV;
    glm::vec4 tangent;
};

enum class PointLightType : std::uint8_t
{
    STATIC,
    MOVING,
    TEMPORARY,
    NON_SHADOW,
};

struct LightUBO
{
    uint32_t numPointLights = 0;
    uint32_t numMovingPointLights = 0;
    uint32_t numSpotLights = 0;
    float pointLightFarPlane = 7500.0f;
    float pointLightNearPlane = 1.0f;
    uint32_t _pad0[3];
    PointLight pointLights[MAX_POINT_LIGHTS];
    PointLight movingPointLights[MAX_MOVING_POINT_LIGHTS];
};
/// @brief Graphics-team's work-in-progress SDL3 GPU renderer.
///
/// Pass architecture (target — current implementation only covers a subset):
///   Pass 0 — Shadow map        (depth-only directional, cascaded)         [TODO]
///   Pass 1 — Skybox            (procedural / cubemap)                     [TODO]
///   Pass 2 — Geometry          (static models + entity models + weapon)   [partial]
///   Pass 3 — Skinned chars     (instanced GPU LBS for players)            [TODO]
///   Pass 4 — Particles         (delegated to ParticleSystem::render)      [TODO]
///   Pass 5 — Post (SSAO/bloom/SSR/volumetrics/tonemap)                    [TODO]
///   Pass 6 — HUD + ImGui                                                  [partial]
///
/// Shaders: `shaders-new/geometry.{vert,frag}` + `shaders-new/hud.{vert,frag}`.
/// Graphics team adds new shaders alongside these as features come online.
class NewRenderer
{
public:
    // ─── Lifecycle ───────────────────────────────────────────────────────────

    /// @brief Initialise the GPU device, pipelines, and default scene assets.
    /// @param window  SDL window to render into.
    /// @return True on success.
    bool init(SDL_Window* window);

    /// @brief Render one frame from the given camera pose.
    /// @param eye    Camera world position.
    /// @param yaw    Horizontal rotation in radians.
    /// @param pitch  Vertical rotation in radians.
    /// @param roll   Camera roll in radians (default 0).
    ///
    /// Reads all per-frame state previously captured by `set*()` calls.  Game
    /// code is expected to call every relevant setter BEFORE calling drawFrame.
    void drawFrame(glm::vec3 eye, float yaw, float pitch, float roll);

    /// @brief Release all GPU resources and shut down the renderer.
    void quit();

    // ─── Readback (must return real values — gameplay depends on them) ───────

    /// @brief Returns the SDL GPU device.  Valid between `init()` and `quit()`.
    ///
    /// Used by sub-renderers (`ParticleSystem`, `HudRenderer`) and tests that
    /// need to allocate GPU resources on the same device.
    [[nodiscard]] SDL_GPUDevice* getDevice() const { return device_; }

    /// @brief Shader format selected during `init()` (SPIR-V on Vulkan, MSL on Metal, DXIL on D3D12).
    ///
    /// Sub-renderers consult this to pick the right precompiled shader binary.
    [[nodiscard]] SDL_GPUShaderFormat getShaderFormat() const { return shaderFormat_; }

    /// @brief Color format of the active swapchain render pass.
    ///
    /// Sub-renderers that draw inside NewRenderer's main pass must create
    /// pipelines with this format so backends such as Metal can validate the
    /// pipeline against the render-pass texture.
    [[nodiscard]] SDL_GPUTextureFormat getSwapchainColorFormat() const { return colorTarget_; }

    /// @brief Current camera (updated every `drawFrame` call).
    ///
    /// Game code reads this for projection-dependent operations: picking,
    /// reticle world-ray, frustum culling, etc.  Caller must not mutate.
    [[nodiscard]] const NewCamera& getCamera() const { return camera_; }

    /// @brief Number of models currently registered in the asset map.
    ///
    /// Used by the debug UI to display loaded-model counts.  Backed by
    /// `Asset::models_`, which is populated by `loadSceneModel()`.
    [[nodiscard]] int modelCount() const;

    /// @brief Legacy HDR colour render target format.
    ///
    /// NewRenderer currently draws directly into the swapchain, so
    /// sub-renderers registered with it should use getSwapchainColorFormat().
    /// Keep this for code paths that actually render into an HDR intermediate.
    [[nodiscard]] static constexpr SDL_GPUTextureFormat getHdrFormat()
    {
        return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
    }

    /// @brief Most-recent `SDL_AcquireGPUCommandBuffer` cost in milliseconds.
    ///
    /// Captured by `drawFrame()`.  Read by the debug HUD's frame-timing panel.
    [[nodiscard]] float getLastAcquireMs() const { return lastAcquireMs_; }

    /// @brief Most-recent command-recording cost (between acquire and submit) in ms.
    [[nodiscard]] float getLastRecordMs() const { return lastRecordMs_; }

    /// @brief Most-recent `SDL_SubmitGPUCommandBuffer` cost in milliseconds.
    [[nodiscard]] float getLastSubmitMs() const { return lastSubmitMs_; }

    // ─── Per-frame submit setters (data-capture stubs) ───────────────────────
    //
    // Called by Game.cpp every frame BEFORE drawFrame.  Each stores the
    // payload into a member field; drawFrame reads it.  Currently most of
    // these have no consumer — the data is captured but nothing renders.

    /// @brief Set the list of entity render commands for this frame.
    /// @param entityList  One entry per visible static/dynamic entity.  Moved-in.
    ///
    /// IMPLEMENTATION: Currently captures into `entities_` and the existing
    /// `drawEntityModels()` loop draws them with the geometry pipeline.  When
    /// adding lighting/tinting/material variation this is the call site whose
    /// data needs to surface in the shader.
    ///
    /// DATA SOURCE: built in Game.cpp's draw-list builder from ECS view
    /// `<Position, Velocity, Renderable, ...>` per frame.
    void setEntityRenderList(std::vector<EntityRenderCmd>&& entityList);

    /// @brief Set dynamic point lights for this frame.
    /// @param pointLights  Up to ~6 lights; renderer may cap silently.
    ///
    /// IMPLEMENTATION: store in `pointLights_`, then push as a UBO array to
    /// the PBR fragment shader during the geometry pass.  Currently a no-op
    /// stub — `pointLights_` is captured but never read.
    ///
    /// DATA SOURCE: built in Game.cpp from ECS each frame (glow-emitting
    /// projectiles, muzzle flashes, etc).
    void setPointLights(std::vector<PointLight> pointLights);
    void setStaticPointLights(std::vector<PointLight>&& pointLights);

    /// @brief Set the first-person weapon viewmodel for this frame.
    /// @param vm  Model handle + viewmodel-space transform + visibility.
    ///
    /// IMPLEMENTATION: Captured into `weapon_`.  `drawWeapon()` reads it and
    /// issues a draw call with the existing geometry pipeline.  Note: the
    /// viewmodel is drawn LAST so it sits on top of the world (no depth
    /// fighting with map geometry near the camera).
    ///
    /// DATA SOURCE: Game.cpp's `runWeaponViewmodel()` step, which composes
    /// the transform from camera state + recoil/sway curves.
    void setWeaponViewmodel(const WeaponViewmodel& vm);

    /// @brief Override the emissive colour of every mesh in a registered model.
    /// @param modelIdUnsanitized  Index returned by `loadSceneModel`/`uploadSceneModel`.
    /// @param emissiveColor       RGB = emissive linear colour, A = ignored.
    ///
    /// IMPLEMENTATION: store overrides keyed by modelIndex in a map; consult
    /// the map when building per-mesh material UBOs inside `drawModel()`.
    /// Currently a no-op.
    ///
    /// DATA SOURCE: Game.cpp uses this to pulse beam/cylinder/sphere glow
    /// (`Game.cpp` legacy: `renderer.setModelEmissive(glowCylinderModelIdx_, ...)`).
    void setModelEmissive(int32_t modelIdUnsanitized, glm::vec4 emissiveColor);

    /// @brief Toggle whether a model is drawn during the scene pass.
    /// @param modelIndex   Renderer-side handle.
    /// @param drawInScene  true = draw as static world geometry; false = only via EntityRenderCmd.
    ///
    /// IMPLEMENTATION: sets `Asset::ModelInstance::drawInScenePass` on every
    /// instance backed by `modelIndex`.  The scene-pass loop in `drawWorldModelInstances`
    /// already honours this flag.
    ///
    /// DATA SOURCE: Game.cpp calls this once per static map asset right after
    /// `loadSceneModel`.
    void setModelScenePass(int32_t modelIndex, bool drawInScene);

    // ─── Skinned characters (animated players) ───────────────────────────────
    //
    // The skinning pipeline (rig install, per-frame palette upload, instanced
    // draw, shaders) is its own subsystem — see `SkinnedRenderer.hpp`.
    // Game code talks to it through the accessor below:
    //
    //   renderer.skinned().setRig(meshes, numJoints);            // once
    //   renderer.skinned().setFrame(palette, instances);          // every frame
    //
    // `NewRenderer::drawFrame` automatically drives the matching
    // `uploadFrame()` (before render pass) and `draw()` (inside geometry pass)
    // — game code never calls those directly.

    /// @brief Accessor for the skinned-character subsystem.
    ///
    /// Use to register the shared rig once at startup, and to push the per-
    /// frame palette + instance arrays each frame.  See SkinnedRenderer.hpp
    /// for the full API + data-flow notes.
    [[nodiscard]] SkinnedRenderer& skinned() { return skinnedRenderer_; }
    [[nodiscard]] const SkinnedRenderer& skinned() const { return skinnedRenderer_; }

    // ─── Static models ───────────────────────────────────────────────────────

    /// @brief Load a model from disk and register it in `Asset::models_` + create a default scene instance.
    /// @param filename                Path under the assets dir.
    /// @param pos                     World position of the default scene instance.
    /// @param scale                   Uniform scale of the default scene instance.
    /// @param flipUVs                 True to flip V coordinate on import (glTF vs DCC convention).
    /// @param excludeNodesContaining  (Unused on main today.)  Substring filter to drop collision-only nodes.
    /// @return Index into `Asset::modelInstances_` (-1 on failure).  Game.cpp stashes this and uses it as a handle.
    ///
    /// IMPLEMENTATION (real):  delegates to `AssetLoader::loadModel`, then
    /// creates an `Asset::ModelInstance` at `pos`, then uploads mesh VB/IB.
    /// See cpp.
    ///
    /// DATA SOURCE: Game.cpp's init phase, once per map asset.
    int loadSceneModel(
        const char* filename, glm::vec3 pos, float scale, bool flipUVs, const std::string& excludeNodesContaining = "");

    /// @brief Set the HUD overlay texture to blit after the geometry pass.
    /// @param hudTexture  HUD's final colour texture (RGBA8 swap-chain-format).  May be null = no HUD.
    ///
    /// IMPLEMENTATION (real): captured into `hudTexture_`; `drawHud()` samples
    /// it in the UI pass.  Sampling is `linear repeat` via `hudSampler_`.
    ///
    /// DATA SOURCE: `Hud::getOutputTexture()` after `Hud::draw()` populates it.
    void setHudTexture(SDL_GPUTexture* hudTexture);

    // ─── Subsystem registration ─────────────────────────────────────────────

    /// @brief Register the particle system so the renderer can call its render hooks.
    /// @param ps  Pointer to ParticleSystem owned by Game.  May be null = disable particles.
    ///
    /// IMPLEMENTATION: store in `particleSystem_`.  During `drawFrame`, the
    /// renderer should call (in this order, inside the main HDR pass):
    ///   ps->uploadToGpu(cmd);     // BEFORE the render pass begins
    ///   ps->render(pass, cmd);    // INSIDE the HDR render pass
    /// Currently a no-op pointer-only stub.
    ///
    /// DATA SOURCE: Game.cpp owns the `ParticleSystem` instance and registers
    /// it once after `particleSystem.init(...)` succeeds.
    void setParticleSystem(ParticleSystem* ps);

    // ─── Settings / toggles ─────────────────────────────────────────────────

    /// @brief Enable or disable vertical sync.
    /// @param enabled  True = VSync on (capped at refresh rate); false = uncapped.
    /// @return True on success.
    ///
    /// IMPLEMENTATION: call `SDL_SetGPUSwapchainParameters` with
    /// `SDL_GPU_PRESENTMODE_VSYNC` vs `SDL_GPU_PRESENTMODE_IMMEDIATE` (or
    /// `MAILBOX` for adaptive).  Currently a no-op — stores `vsyncEnabled_`
    /// but doesn't apply.
    ///
    /// DATA SOURCE: Game.cpp toggles this when entering/leaving menus or via
    /// debug UI.
    bool setVSync(bool enabled);

    /// @brief Request a screenshot to be saved to disk after the next frame.
    /// @param path  Output file path (PNG).
    ///
    /// IMPLEMENTATION: after `drawFrame`'s submit + present, read back the
    /// swapchain texture into a transfer buffer, map it CPU-side, write a PNG
    /// via `stbi_write_png`.  Currently a no-op — `pendingScreenshotPath_`
    /// is captured but not consumed.
    ///
    /// DATA SOURCE: debug UI hotkey / console command.
    void requestScreenshot(const std::string& path);

    /// @brief Queue a vertex-buffer re-upload for one mesh of a loaded model.
    /// @param modelIndex  Renderer-side model handle.
    /// @param meshIndex   Mesh index within that model.
    /// @param vertices    New vertex data (caller retains ownership — copy if needed).
    /// @param vertexCount Number of vertices in `vertices`.
    ///
    /// IMPLEMENTATION: legacy used this for CPU-skinning entity meshes (now
    /// superseded by `setSkinnedFrame`).  May not need to come back; leave as
    /// a no-op stub for API parity.  If a use-case re-emerges, do the copy
    /// inside the next `drawFrame`'s copy pass.
    ///
    /// DATA SOURCE: legacy CPU LBS path (now defunct).  No current call site.
    void updateModelMeshVertices(int modelIndex, int meshIndex, const Vertex* vertices, Uint32 vertexCount);

    /// @brief Load an equirectangular HDR image as the environment skybox + IBL source.
    /// @param path  Absolute or assets-relative path to a .hdr file.
    /// @return True on success.
    ///
    /// IMPLEMENTATION: load via stb_image (float), upload as a 2D HDR texture,
    /// run an equirect→cubemap compute pass, derive the irradiance + prefilter
    /// cubemaps for IBL.  Currently a no-op stub.
    ///
    /// DATA SOURCE: debug UI's HDR picker — user selects from `availableHDRFiles_`.
    bool loadHDRSkybox(const std::string& path);

    /// @brief Scan the assets HDR directory and populate `availableHDRFiles_`.
    ///
    /// IMPLEMENTATION: iterate `assets/hdr/*.hdr` via std::filesystem, fill
    /// the vector with the paths.  Currently a no-op.
    ///
    /// DATA SOURCE: called once at init; debug UI reads `availableHDRFiles_`
    /// to populate its dropdown.
    void scanHDRFiles();

    bool setRig(const std::vector<RigMeshSource>& meshes, int numJoints);

    void setSkinnedFrame(const std::vector<glm::mat4>& palette, const std::vector<SkinnedInstance>& instances);

    bool setViewmodelRig(const std::vector<RigMeshSource>& meshes, int numJoints);
    void setViewmodelFrame(const std::vector<glm::mat4>& palette, const std::vector<SkinnedInstance>& instances);
    void setViewmodelTexture(int modelInstanceIndex);
    bool setViewmodelArmsRig(const std::vector<RigMeshSource>& meshes, int numJoints);
    void setViewmodelArmsFrame(const std::vector<glm::mat4>& palette,
                               const std::vector<SkinnedInstance>& instances);
    void setViewmodelArmsTexture(int modelInstanceIndex);
    // ─── Public settings members (mutable directly from Game / debug UI) ─────
    //
    // The legacy renderer exposed these as direct member access.  Keep the
    // pattern so Game.cpp and the debug UI can read/write without a getter
    // ceremony.

    float renderScale = 1.0f;                    ///< Internal-resolution multiplier (0.5 = half-res, 2.0 = SSAA).
    float mainHorizontalFovDegrees = 110.0f;     ///< Main camera horizontal field of view in degrees.
    float scopeZoom = 1.0f;                      ///< Per-frame scope zoom multiplier (FOV divisor).
                                                 ///< 1.0 = no zoom; 1.5 = ADS through the charge rifle scope (FOV/1.5).
                                                 ///< Game.cpp drives this each frame from the local player's ADS state.
    bool imguiEnabled = true;                    ///< Master toggle for the ImGui debug overlay.
    RenderToggles toggles{};                     ///< Per-pass on/off toggles (see RenderToggles in RendererTypes.hpp).
    float hdrExposure = 1.0f;                    ///< Debug exposure multiplier used by the tonemap pass.
    float hdrWhitePoint = 4.0f;                  ///< Debug white point for Extended Reinhard tonemapping.
    std::vector<std::string> availableHDRFiles;  ///< Filled by `scanHDRFiles()`; consumed by debug UI.
    std::string currentHDRName = "(procedural)"; ///< Display name of the currently-loaded HDR.
    bool useHDRSkybox = false;                   ///< True after a successful `loadHDRSkybox()`.

private:
    // ─── Existing internal helpers ───────────────────────────────────────────

    bool createGeometryPipeline();
    SDL_GPUGraphicsPipeline* createDepthPipeline(const SDL_GPURasterizerState& rasterizer_state) const;
    bool createDepthRes0Pipeline();
    bool createDepthRes1Pipeline();
    bool createDepthRes2Pipeline();
    bool createHudPipeline();
    bool createFxaaPipeline();
    bool createTonemapPipeline();
    bool ensureDepthTextureSize(Uint32 width, Uint32 height);
    bool ensureSceneTextureSize(Uint32 width, Uint32 height);
    void createMeshBuffers(MeshIdInt meshId) const;
    void setMainCamera(glm::vec3 eye, float yaw, float pitch, float roll, Uint32 width, Uint32 height, float fov);

    void drawGeometryDepthPass(SDL_GPUTexture* depthTexture,
                               Uint8 res,
                               Uint8 layer,
                               SDL_GPUCommandBuffer* cmd,
                               const glm::mat4& shadowViewProjection,
                               bool staticGeometry,
                               bool entityGeometry,
                               bool skinnedGeometry);
    void drawToShadowMap(SDL_GPUCommandBuffer* cmd,
                         SDL_GPUTexture* shadowMapTexture,
                         Uint8 res,
                         bool staticGeometry,
                         bool entityGeometry,
                         bool skinnedGeometry,
                         PointLightType lightType);

    void onFirstFrame(SDL_GPUCommandBuffer* cmd);

    void bindLightShadowInfo(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);
    void drawGeometryPass(SDL_GPUTexture* sceneColor, SDL_GPUCommandBuffer* cmd);
    void drawUIPass(SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd);
    void drawHudPass(SDL_GPUTexture* target, SDL_GPUCommandBuffer* cmd);
    void drawFxaaPass(SDL_GPUTexture* sceneColor, SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd);
    void drawTonemapPass(SDL_GPUTexture* hdrSceneColor, SDL_GPUTexture* ldrColor, SDL_GPUCommandBuffer* cmd);
    void drawParticles(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd) const;
    void drawWeaponPass(SDL_GPUTexture* sceneColor, SDL_GPUCommandBuffer* cmd);
    void drawWorldModelInstances(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd, bool depth, const FrustumPlanes& frustumPlanes);
    void drawWeapon(SDL_GPURenderPass* geometryPass, SDL_GPUCommandBuffer* cmd, const FrustumPlanes& frustumPlanes);
    void drawSkinnedModels(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);

    void drawModel(ModelIdInt modelId,
                   const glm::mat4& modelTransform,
                   SDL_GPURenderPass* renderPass,
                   SDL_GPUCommandBuffer* cmd,
                   const FrustumPlanes& frustumPlanes);


    void drawModelDepth(ModelIdInt modelId,
                        const glm::mat4& modelTransform,
                        SDL_GPURenderPass* renderPass,
                        SDL_GPUCommandBuffer* cmd,
                        const FrustumPlanes& frustumPlanes);

    void drawEntityModels(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd, bool depth,const FrustumPlanes& frustumPlanes);

    void drawMesh(SDL_GPURenderPass* renderPass, const Asset::Mesh& mesh) const;
    void drawHud(SDL_GPURenderPass* pass);

    static bool inFrustum(const Asset::AABB &modelElementAABB,const FrustumPlanes &frustumPlanes,const glm::mat4 &modelMat);

    // ─── Member state ────────────────────────────────────────────────────────

    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;

    SDL_GPUGraphicsPipeline* geometryPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* hudPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* fxaaPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* tonemapPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* skinnedPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* depthRes0Pipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* depthRes1Pipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* depthRes2Pipeline_ = nullptr;

    SDL_GPUTextureFormat colorTarget_ = SDL_GPU_TEXTUREFORMAT_INVALID;
    SDL_GPUTexture* sceneColor_ = nullptr;
    SDL_GPUTexture* tonemappedColor_ = nullptr;
    SDL_GPUDepthStencilTargetInfo depthTarget_{};
    Uint32 sceneWidth_ = 0;
    Uint32 sceneHeight_ = 0;
    Uint32 depthWidth_ = 0;
    Uint32 depthHeight_ = 0;

    // Default fallback texture used when a mesh has no material/texture.
    SDL_GPUTexture* texture_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;

    SDL_GPUTexture* hudTexture_ = nullptr;
    SDL_GPUSampler* hudSampler_ = nullptr;
    SDL_GPUSampler* fxaaSampler_ = nullptr;

    // constexpr uint32_t shadowSize = 2048;
    //  constexpr uint32_t shadowSize = 512;
    static const uint32_t shadowSize = 1024;
    static const uint32_t macShadowSize = 512;
    static const uint32_t staticShadowSize = 2048;
    SDL_GPUTexture* dynamicShadowMaps_ = nullptr;
    SDL_GPUTexture* staticShadowMaps_ = nullptr;
    SDL_GPUSampler* staticDepthSampler_ = nullptr;
    SDL_GPUSampler* dynamicDepthSampler_ = nullptr;


    SDL_GPUTexture* movingLightShadowMaps_ = nullptr;

    glm::vec3 cubeFaceTargets_[NUM_CUBE_FACES];
    glm::vec3 cubeFaceUps_[NUM_CUBE_FACES];

    bool firstFrame_ = true;

    LightUBO sceneLightInfo_;

    NewCamera camera_;

    // Per-frame captured state ───────────────────────────────────────────────
    std::vector<EntityRenderCmd> entities_;
    // std::vector<Asset::AABB> entityAABBs_;
    WeaponViewmodel weapon_{};
    ParticleSystem* particleSystem_ = nullptr;

    // Settings captured state ─────────────────────────────────────────────────
    bool vsyncEnabled_ = true;
    std::string pendingScreenshotPath_;

    // Skinned-character subsystem (see SkinnedRenderer.hpp) ──────────────────
    SkinnedRenderer skinnedRenderer_;
    SkinnedRenderer viewmodelSkinned_;
    SkinnedRenderer viewmodelArmsSkinned_;

    // Telemetry counters (filled by drawFrame) ────────────────────────────────
    float lastAcquireMs_ = 0.0f;
    float lastRecordMs_ = 0.0f;
    float lastSubmitMs_ = 0.0f;
};
