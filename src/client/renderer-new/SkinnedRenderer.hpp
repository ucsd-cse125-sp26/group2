/// @file SkinnedRenderer.hpp
/// @brief Self-contained renderer subsystem for skinned (animated) characters.
///
/// Owns every GPU resource and CPU staging buffer needed to draw animated
/// players via instanced GPU linear-blend skinning.  Decoupled from
/// `NewRenderer` so the graphics team can iterate on the animation pipeline
/// (rig install, palette upload, draw call, shaders) without touching the
/// main renderer's plumbing.
///
/// Lifecycle, owned by `NewRenderer`:
///   1. NewRenderer::init()      → SkinnedRenderer::init(device)
///   2. Game.cpp (once)          → renderer.skinned().setRig(meshes, numJoints)
///   3. Game.cpp (every frame)   → renderer.skinned().setFrame(palette, instances)
///   4. NewRenderer::drawFrame   → SkinnedRenderer::uploadFrame(cmd, copyPass)  [BEFORE render pass]
///                                  SkinnedRenderer::draw(renderPass, cmd)      [INSIDE render pass]
///   5. NewRenderer::quit()      → SkinnedRenderer::shutdown()
///
/// GPU pipeline state owned here:
///   - per-mesh `vb` (ModelVertex), `boneVb` (BoneInfluence), `ib` (uint32 indices)
///   - palette SSBO (mat4[numInstances*numJoints])
///   - instances SSBO (SkinnedInstance[numInstances])
///   - paired transfer buffers for per-frame upload (grown on demand)
///   - the skinned pipeline + shaders (graphics team adds these — currently null)
///
/// Search `TODO(graphics)` in the cpp to find every hook still awaiting work.

#pragma once

#include "Camera.hpp"       // FrustumPlanes
#include "RendererTypes.hpp" // BoneInfluence, SkinnedInstance, RigMeshSource

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>
#include <vector>

/// @brief Instanced GPU-skinning subsystem.  One per renderer.
class SkinnedRenderer
{
public:
    SkinnedRenderer() = default;
    ~SkinnedRenderer() = default;

    SkinnedRenderer(const SkinnedRenderer&) = delete;
    SkinnedRenderer& operator=(const SkinnedRenderer&) = delete;
    SkinnedRenderer(SkinnedRenderer&&) = delete;
    SkinnedRenderer& operator=(SkinnedRenderer&&) = delete;

    /// @brief Bind the SDL GPU device.  Call from `NewRenderer::init` once
    /// the device exists.  Does NOT allocate any GPU resources yet; those
    /// are created lazily on the first `setRig` / `setFrame` call.
    /// @param device  Borrowed; the device must outlive this SkinnedRenderer.
    void init(SDL_GPUDevice* device,
              SDL_GPUTextureFormat& colorTarget,
              const SDL_GPUShaderFormat& shaderFormat);

    /// @brief Enable/disable per-instance frustum culling in setFrame. Defaults
    /// to true.
    void setFrustumCullEnabled(bool enabled) { frustumCullEnabled_ = enabled; }

    /// @brief Install the shared character rig.  Call ONCE after `init`.
    /// @param meshes     One source-mesh entry per skinned mesh in the rig (typically 1-3 for humanoid rigs).
    /// @param numJoints  Number of skeleton joints.  Determines per-instance palette stride in the shader.
    /// @return True on success.  False if already installed, or upload failed.
    ///
    /// IMPLEMENTATION (real, wired):
    ///   - Iterates `meshes`, creating three GPU buffers per mesh:
    ///       slot 0: bind-pose vertices (`ModelVertex`, 48 bytes/vert)   — VERTEX usage
    ///       slot 1: bone influences   (`BoneInfluence`, 32 bytes/vert)  — VERTEX usage
    ///       slot 2: indices            (uint32_t, 4 bytes each)         — INDEX usage
    ///     Saves them in `skinnedMeshes_`.
    ///   - Stores `numJoints` in `numJoints_`; sets `rigInstalled_`.
    ///   - Does NOT allocate palette/instance SSBOs — those grow lazily on
    ///     the first `setFrame` call.
    ///
    /// DATA SOURCE: Game.cpp builds `vector<RigMeshSource>` from
    /// `CharacterRig::meshes()` (animation/CharacterRig.hpp) once after the
    /// rig FBX loads.  See `RigMeshSource` in RendererTypes.hpp for the layout.
    bool setRig(const std::vector<RigMeshSource>& meshes, int numJoints);

    /// @brief Push this frame's per-character bone palette + per-instance data
    /// and frustum-cull it down to the on-screen subset.
    /// @param palette    Flat array, sized `numInstances * numJoints` (mat4 each).
    /// @param instances  One entry per character (ALL of them — visible or not).
    ///                   `paletteBase` of entry `i` MUST equal `i * numJoints` so
    ///                   the palette slice for that instance can be located.
    /// @param frustum    Camera view-projection frustum planes (Gribb-Hartmann).
    ///                   Each instance is bounding-sphere tested against these and
    ///                   dropped if fully outside the view.
    ///
    /// IMPLEMENTATION (real, wired):
    ///   - Sphere-culls `instances` against `frustum`: the bounding sphere is the
    ///     rig's bind-pose bounds (computed in `setRig`) transformed by the
    ///     instance `worldTransform`, so the test tracks the actual rendered mesh
    ///     (which is vertically offset from the sim position) rather than a bare
    ///     point — this is what fixes characters popping out at the screen edge.
    ///   - Copies the surviving instances (and their palette slices, compacted so
    ///     no off-screen palettes are uploaded) into CPU-side staging
    ///     (`framePalette_`, `frameInstances_`).
    ///   - The actual GPU upload happens later via `uploadFrame()` (called by
    ///     `NewRenderer::drawFrame` before the render pass starts).
    ///
    /// DATA SOURCE: Game.cpp's per-frame animation block walks the ECS view
    /// `<AnimatedCharacter, Position, Velocity, PlayerVisState, InputSnapshot>`:
    ///   1. For each character compute `worldTransform = T(pos) * R(yaw) * S(scale)`.
    ///   2. `palette[slot * numJoints .. (slot+1) * numJoints] = animator.skinMatrices()`.
    ///   3. `instances[slot] = {worldTransform, paletteBase=slot*numJoints, tint}`.
    ///
    /// CONSUMER: the skinned vertex shader reads:
    ///   `instances[gl_InstanceIndex]`           → worldTransform + paletteBase
    ///   `palette[paletteBase + boneIndices.k]`  → bone matrix k (k = 0..3)
    void setFrame(const std::vector<glm::mat4>& palette,
                  const std::vector<SkinnedInstance>& instances,
                  const FrustumPlanes& frustum);

    /// @brief Upload this frame's palette + instance buffers to the GPU.
    /// @param cmd       Frame command buffer.
    /// @param copyPass  An open copy pass on `cmd`.
    ///
    /// Called from `NewRenderer::drawFrame` BEFORE the main render pass
    /// begins so the copy is sequenced ahead of the draws.  Uses `cycle=true`
    /// on the transfer-buffer map so we never stall waiting on last frame's
    /// GPU read of the SSBO.
    void uploadFrame(SDL_GPUCommandBuffer* cmd, SDL_GPUCopyPass* copyPass);

    /// @brief Issue the instanced draws for every visible skinned character.
    /// @param renderPass  The active main HDR (or swapchain) render pass.
    /// @param cmd         The frame command buffer.
    ///
    /// Called from `NewRenderer::drawFrame` INSIDE the geometry pass.
    /// Currently a no-op placeholder — see cpp for the algorithm sketch.
    void draw(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);

    /// @brief Issue the instanced draws for every visible skinned character
    /// into a depth-only shadow pass.
    /// @param renderPass  The active shadow depth render pass.
    /// @param cmd         The frame command buffer.
    ///
    /// Mirrors `draw()` but binds the depth-only pipeline so the player rig
    /// is rasterised into the shadow map (and thus casts shadows).  Relies on
    /// the caller having already pushed the shadow view-projection at vertex
    /// UBO slot 0 (NewRenderer::drawGeometryDepthPass does this).
    void drawDepth(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);

    /// @brief Draw wallhack chams for flagged instances.
    ///
    /// Material id 2 renders flat-red with a GREATER depth test, so only the
    /// parts occluded by world geometry show through. Must be called BEFORE
    /// `draw()` so the depth buffer still holds world-only geometry.
    void drawChams(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);

    /// @brief Draw the killcam killer full-body red highlight.
    ///
    /// Material id 1 renders after the normal skinned pass so the killer body is
    /// visibly red in the killcam, matching the weapon highlight.
    void drawKillcamHighlight(SDL_GPURenderPass* renderPass, SDL_GPUCommandBuffer* cmd);

    /// @brief Release all GPU resources owned by this subsystem.
    /// Called from `NewRenderer::quit` (BEFORE the device is destroyed).
    void shutdown();

    // ─── Readback (optional, for debug UI) ───────────────────────────────────

    /// @brief Number of joints in the installed rig (0 if not installed).
    [[nodiscard]] int numJoints() const { return numJoints_; }

    /// @brief True after a successful `setRig`.
    [[nodiscard]] bool rigInstalled() const { return rigInstalled_; }

    /// @brief Number of instances pending render this frame (0 if no frame submitted).
    [[nodiscard]] size_t pendingInstanceCount() const { return frameInstances_.size(); }

private:
    /// @brief One mesh of the installed skinned rig.  Built by `setRig`.
    ///
    /// `vb`     carries `ModelVertex`   (48 B/vert) at vertex buffer slot 0.
    /// `boneVb` carries `BoneInfluence` (32 B/vert) at vertex buffer slot 1.
    /// `ib`     is a 32-bit index buffer.
    /// Both vertex arrays are parallel — vertex `i` of `vb` uses bone
    /// influences `i` of `boneVb`.
    struct SkinnedMesh
    {
        SDL_GPUBuffer* vb = nullptr;
        SDL_GPUBuffer* boneVb = nullptr;
        SDL_GPUBuffer* ib = nullptr;
        Uint32 indexCount = 0;
        Uint32 vertexCount = 0;
    };

    struct SsboInfo
    {
        SDL_GPUBuffer* ssbo_{};
        Uint32 capacityBytes_ = 0;
    };

    /// @brief Grow palette/instance SSBOs (and their transfer buffers) to at least these byte sizes.
    bool ensureSsbos(Uint32 paletteBytes, Uint32 instanceBytes);

    /// @brief Test whether a world-space bounding sphere intersects (or lies
    /// inside) the view frustum.  Uses the Gribb-Hartmann plane convention of
    /// `FrustumPlanes` (a point is inside a plane when `dot(plane.xyz, p) +
    /// plane.w >= 0`).  Returns false only when the sphere is fully outside at
    /// least one plane.
    static bool sphereInFrustum(const glm::vec3& center, float radius, const FrustumPlanes& planes);

    bool createSkinningPipeline(SDL_GPUTextureFormat& colorTarget, const SDL_GPUShaderFormat& shaderFormat);

    bool createSkinnedDepthPipeline(const SDL_GPUShaderFormat& shaderFormat);

    /// @brief Create the wallhack chams pipeline.
    bool createChamsPipeline();

    /// @brief Create the killcam full-body highlight pipeline.
    bool createKillcamHighlightPipeline();

    // ─── Borrowed ────────────────────────────────────────────────────────────
    SDL_GPUDevice* device_ = nullptr;

    // ─── Owned: pipeline (graphics team to create) ───────────────────────────
    /// @brief The skinned graphics pipeline.  TODO(graphics): create this in
    /// `init` (or lazily on first frame) with two vertex buffers (ModelVertex,
    /// BoneInfluence), two vertex storage buffers (palette, instances), one
    /// vertex UBO (view-projection), depth test on, cull mode NONE.
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;

    /// @brief Depth-only variant of the skinned pipeline, used to rasterise
    /// the rig into the shadow map so the player casts a shadow.
    SDL_GPUGraphicsPipeline* depthPipeline_ = nullptr;

    /// @brief Wallhack chams pipeline: flat-red, GREATER depth test, no depth
    /// write — draws flagged players where they're occluded by world geometry.
    SDL_GPUGraphicsPipeline* chamsPipeline_ = nullptr;

    /// @brief Killcam full-body red highlight pipeline.
    SDL_GPUGraphicsPipeline* killcamHighlightPipeline_ = nullptr;

    /// @brief Cached formats (from init) needed to build the chams pipeline.
    SDL_GPUTextureFormat colorFormat_{};
    SDL_GPUShaderFormat shaderFormat_{};

    /// @brief Indices into `frameInstances_` of wallhack-revealed enemies.
    std::vector<Uint32> chamsIndices_;

    /// @brief Indices into `frameInstances_` of killcam-highlighted killers.
    std::vector<Uint32> killcamHighlightIndices_;

    // ─── Owned: rig (set once via setRig) ────────────────────────────────────
    bool rigInstalled_ = false;
    int numJoints_ = 0;
    std::vector<SkinnedMesh> skinnedMeshes_;
    // Disabled by default: Game submits the skinned frame before drawFrame()
    // updates the renderer camera, so culling here can use a stale frustum and
    // make remote players flicker/fade at the edge of the previous frame's view.
    bool frustumCullEnabled_ = false; ///< Optional per-instance frustum cull in setFrame.

    // Bind-pose bounding sphere (rig-local space), computed in `setRig` and used
    // by `setFrame` to frustum-cull instances.  The radius carries an animation
    // margin so limbs swung out past the bind pose are not clipped early.
    glm::vec3 rigBoundingCenter_{0.0f};
    float rigBoundingRadius_ = 0.0f;

    // ─── Owned: per-frame GPU resources (grow on demand) ─────────────────────
    // SDL_GPUBuffer* palettesSsbo_ = nullptr;  ///< STORAGE_READ, mat4[numInstances * numJoints].
    // SDL_GPUBuffer* instancesSsbo_ = nullptr; ///< STORAGE_READ, SkinnedInstance[numInstances].
    // Uint32 palettesCapacityBytes_ = 0;
    // Uint32 instancesCapacityBytes_ = 0;
    SsboInfo palettesSsboInfo_;
    SsboInfo instancesSsboInfo_;

    SDL_GPUTransferBuffer* paletteXfer_ = nullptr;
    SDL_GPUTransferBuffer* instanceXfer_ = nullptr;
    Uint32 paletteXferCapacityBytes_ = 0;
    Uint32 instanceXferCapacityBytes_ = 0;

    // ─── CPU staging (filled by setFrame, drained by uploadFrame) ────────────
    //
    // `frameInstances_` holds EVERY submitted character, partitioned so the
    // on-screen ones come first:
    //   [0, visibleInstanceCount_)        → inside the camera frustum
    //   [visibleInstanceCount_, size())   → off-screen, but still shadow casters
    // The colour pass (`draw`) renders only the front slice; the shadow/depth
    // pass (`drawDepth`) renders the whole buffer so off-screen players keep
    // casting shadows.  `framePalette_` is compacted in the same order.
    std::vector<glm::mat4> framePalette_;
    std::vector<SkinnedInstance> frameInstances_;
    Uint32 visibleInstanceCount_ = 0;
    bool frameDirty_ = false;
};
