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
#include "Camera.hpp"
#include "RendererTypes.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <string>
#include <vector>

class ParticleSystem; ///< Forward-declared — owned by Game, registered via setParticleSystem().

/// @brief Vertex attribute layout for the static geometry pipeline.
///
/// 32 bytes: position (12) + normal (12) + texUV (8).  NOTE: this is DISTINCT
/// from `ModelVertex` (48 bytes, includes tangent) used by the skinned-rig
/// pipeline.  Static meshes use this; skinned characters use `ModelVertex`.
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texUV;
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

    /// @brief Format of the HDR colour render target.
    ///
    /// Sub-renderers that render INTO the main HDR pass (particles, beams,
    /// glow) must declare this as their pipeline's colour target format.
    /// Returning RGBA16F matches the legacy renderer; graphics team may
    /// change this if they pick a different intermediate format, BUT must
    /// update every sub-renderer pipeline to match.
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
    // Two-call interface mirroring the legacy renderer:
    //   - setSkinnedRig() ONCE at startup → uploads bind-pose mesh + bone-
    //     influence buffer; allocates per-frame SSBOs.
    //   - setSkinnedFrame() EVERY FRAME → uploads a flat bone palette + a
    //     parallel per-instance buffer (worldTransform, paletteBase, tint).
    //
    // CURRENT STATE:
    //   - setSkinnedRig: IMPLEMENTED (GPU upload of mesh + influences).
    //   - setSkinnedFrame: IMPLEMENTED (uploads palette+instance SSBOs each frame).
    //   - The actual instanced draw call: NOT IMPLEMENTED (skinnedPipeline_ is null).
    //
    // To bring skinned characters to screen, graphics team needs to:
    //   1. Add shaders/skinned vertex+fragment (LBS in vertex shader, sample
    //      bonePalette SSBO indexed by paletteBase + boneIndex).
    //   2. Create `skinnedPipeline_` (two vertex buffers: ModelVertex + BoneInfluence;
    //      two SSBOs as vertex storage; one UBO for view/proj).
    //   3. Implement `drawSkinnedCharacters()` (called from drawFrame between
    //      static geometry and the weapon — see the placeholder in cpp).
    //   4. Test by spawning multiple players; one instanced draw per rig mesh
    //      should render all of them.

    /// @brief Install the shared skinned-character rig.  Call ONCE after `init()`.
    /// @param meshes     One source-mesh entry per skinned mesh in the rig (typically 1-3 for humanoid rigs).
    /// @param numJoints  Number of skeleton joints.  Determines per-instance palette stride.
    /// @return True on success.  False if already installed, or upload failed.
    ///
    /// IMPLEMENTATION (real, already wired):
    ///   - Iterates `meshes`, creating two GPU vertex buffers per mesh:
    ///       slot 0: bind-pose vertices (`ModelVertex`, 48 bytes/vert)
    ///       slot 1: bone influences  (`BoneInfluence`, 32 bytes/vert)
    ///     plus an index buffer.  Saves them in `skinnedMeshes_`.
    ///   - Stores `numJoints` in `skinnedNumJoints_`; sets `skinnedRigInstalled_`.
    ///   - Does NOT allocate the palette/instance SSBOs — those are grown
    ///     lazily on the first `setSkinnedFrame` call.
    ///
    /// DATA SOURCE: Game.cpp builds `vector<RigMeshSource>` from
    /// `CharacterRig::meshes()` (animation/CharacterRig.hpp) once after the
    /// rig FBX loads.  See `RigMeshSource` in RendererTypes.hpp for the layout.
    bool setSkinnedRig(const std::vector<RigMeshSource>& meshes, int numJoints);

    /// @brief Push this frame's per-character bone palette + per-instance data.
    /// @param palette    Flat array, sized `numInstances * numJoints` (mat4 each).
    /// @param instances  One entry per visible character.  `paletteBase` of entry
    ///                   `i` MUST equal `i * numJoints` so the shader's index math works.
    ///
    /// IMPLEMENTATION (real, already wired):
    ///   - Copies both vectors into CPU-side staging (`framePalette_`, `frameInstances_`).
    ///   - During `drawFrame` (before the geometry pass), uploads them into
    ///     `palettesSSBO_` and `instancesSSBO_` via transfer buffers with
    ///     cycle=true (so we don't stall on last frame's GPU read).
    ///   - SSBOs grow on demand if more instances/joints appear than fit.
    ///
    /// DATA SOURCE: Game.cpp's per-frame animation block (parallel walk of
    /// ECS view `<AnimatedCharacter, Position, Velocity, PlayerVisState,
    /// InputSnapshot>`):
    ///   1. For each visible character compute `worldTransform = T(pos)*R(yaw)*S(scale)`.
    ///   2. `palette[slot * numJoints .. (slot+1) * numJoints] = animator.skinMatrices()`.
    ///   3. `instances[slot] = {worldTransform, paletteBase=slot*numJoints, tint}`.
    ///
    /// CONSUMER: the (not-yet-written) skinned vertex shader reads
    ///   `instances[gl_InstanceIndex]` for worldTransform + paletteBase
    ///   `palette[paletteBase + boneIndices.k]` weighted by `boneWeights.k`.
    void setSkinnedFrame(const std::vector<glm::mat4>& palette, const std::vector<SkinnedInstance>& instances);

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
    int loadSceneModel(const char* filename,
                       glm::vec3 pos,
                       float scale,
                       bool flipUVs,
                       const std::string& excludeNodesContaining = "");

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

    // ─── Public settings members (mutable directly from Game / debug UI) ─────
    //
    // The legacy renderer exposed these as direct member access.  Keep the
    // pattern so Game.cpp and the debug UI can read/write without a getter
    // ceremony.

    AAMode aaMode = AAMode::SMAA_T2x;          ///< Anti-aliasing mode.  Re-applied at the start of each frame.
    float renderScale = 1.0f;                  ///< Internal-resolution multiplier (0.5 = half-res, 2.0 = SSAA).
    bool imguiEnabled = true;                  ///< Master toggle for the ImGui debug overlay.
    RenderToggles toggles{};                   ///< Per-pass on/off toggles (see RenderToggles in RendererTypes.hpp).
    std::vector<std::string> availableHDRFiles;///< Filled by `scanHDRFiles()`; consumed by debug UI.
    std::string currentHDRName = "(procedural)"; ///< Display name of the currently-loaded HDR.
    bool useHDRSkybox = false;                 ///< True after a successful `loadHDRSkybox()`.

private:
    // ─── Existing internal helpers ───────────────────────────────────────────

    bool createGeometryPipeline();
    bool createHudPipeline();
    bool ensureDepthTextureSize(Uint32 width, Uint32 height);
    void createMeshBuffers(MeshIdInt meshId) const;
    void setMainCamera(glm::vec3 eye, float yaw, float pitch, float roll, Uint32 width, Uint32 height);
    void drawGeometryPass(SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd);
    void drawUIPass(SDL_GPUTexture* swapchain, SDL_GPUCommandBuffer* cmd);
    void drawWorldModelInstances(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);
    void drawWeapon(SDL_GPURenderPass* geometryPass, SDL_GPUCommandBuffer* cmd);
    void drawModel(ModelIdInt modelId,
                   const glm::mat4& modelTransform,
                   SDL_GPURenderPass* renderPass,
                   SDL_GPUCommandBuffer* cmd);
    void drawEntityModels(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);
    void drawMesh(SDL_GPURenderPass* renderPass, const Asset::Mesh& mesh) const;
    void drawHud(SDL_GPURenderPass* pass);

    // ─── Skinned-character internals (NEW) ───────────────────────────────────

    /// @brief One mesh of the installed skinned rig.  Built by `setSkinnedRig`.
    ///
    /// `vb` carries `ModelVertex` (48-byte bind-pose vertices) at vertex buffer slot 0.
    /// `boneVb` carries `BoneInfluence` (32-byte index+weight) at vertex buffer slot 1.
    /// Both are parallel arrays — vertex `i` of `vb` uses bone influences `i` of `boneVb`.
    struct SkinnedMesh
    {
        SDL_GPUBuffer* vb = nullptr;
        SDL_GPUBuffer* boneVb = nullptr;
        SDL_GPUBuffer* ib = nullptr;
        Uint32 indexCount = 0;
        Uint32 vertexCount = 0;
    };

    /// @brief Per-frame palette+instance upload.  Called from `drawFrame` before begin-render-pass.
    /// @param cmd       Frame command buffer.
    /// @param copyPass  An open copy pass on `cmd`.
    void uploadSkinnedFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUCopyPass* copyPass);

    /// @brief Grow palette/instance SSBOs (and their transfer buffers) to at least these byte sizes.
    bool ensureSkinnedSSBOs(Uint32 paletteBytes, Uint32 instanceBytes);

    /// @brief Issue the instanced draws for every skinned character.
    ///
    /// TODO(graphics): Currently a no-op placeholder.  See the doc on
    /// `setSkinnedFrame` for the algorithm to implement.
    void drawSkinnedCharacters(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);

    // ─── Member state ────────────────────────────────────────────────────────

    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUShaderFormat shaderFormat_ = SDL_GPU_SHADERFORMAT_INVALID;

    SDL_GPUGraphicsPipeline* geometryPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* hudPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* skinnedPipeline_ = nullptr; ///< TODO(graphics): create in init (or lazily on first skinned frame).

    SDL_GPUDepthStencilTargetInfo depthTarget_{};
    Uint32 depthWidth_ = 0;
    Uint32 depthHeight_ = 0;

    // Default fallback texture used when a mesh has no material/texture.
    SDL_GPUTexture* texture_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;

    SDL_GPUTexture* hudTexture_ = nullptr;
    SDL_GPUSampler* hudSampler_ = nullptr;

    NewCamera camera_;

    // Per-frame captured state ───────────────────────────────────────────────
    std::vector<EntityRenderCmd> entities_;
    WeaponViewmodel weapon_{};
    std::vector<PointLight> pointLights_;
    ParticleSystem* particleSystem_ = nullptr;

    // Settings captured state ─────────────────────────────────────────────────
    bool vsyncEnabled_ = true;
    std::string pendingScreenshotPath_;

    // Skinned-rig state ──────────────────────────────────────────────────────
    bool skinnedRigInstalled_ = false;
    int skinnedNumJoints_ = 0;
    std::vector<SkinnedMesh> skinnedMeshes_;

    SDL_GPUBuffer* palettesSSBO_ = nullptr;          ///< STORAGE_READ, mat4[numInstances * numJoints].
    SDL_GPUBuffer* instancesSSBO_ = nullptr;         ///< STORAGE_READ, SkinnedInstance[numInstances].
    Uint32 palettesCapacityBytes_ = 0;
    Uint32 instancesCapacityBytes_ = 0;
    SDL_GPUTransferBuffer* paletteXfer_ = nullptr;
    SDL_GPUTransferBuffer* instanceXfer_ = nullptr;
    Uint32 paletteXferCapacityBytes_ = 0;
    Uint32 instanceXferCapacityBytes_ = 0;

    std::vector<glm::mat4> framePalette_;            ///< CPU staging for the next frame's palette.
    std::vector<SkinnedInstance> frameInstances_;    ///< CPU staging for the next frame's instances.
    bool skinnedFrameDirty_ = false;                 ///< True when CPU data has changed since last upload.

    // Telemetry counters (filled by drawFrame) ────────────────────────────────
    float lastAcquireMs_ = 0.0f;
    float lastRecordMs_ = 0.0f;
    float lastSubmitMs_ = 0.0f;
};
