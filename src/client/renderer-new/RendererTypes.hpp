/// @file RendererTypes.hpp
/// @brief Shared data types exchanged between Game (producer) and NewRenderer (consumer).
///
/// Every struct in this file is a pure value type — no GPU handles, no
/// owning pointers, no dependencies on renderer internals.  Game code
/// builds these per frame (or per registration) and hands them off via
/// `NewRenderer::set*()` methods; the renderer then reads them during
/// `drawFrame()`.  Keep it that way: anything that needs GPU handles
/// belongs inside `NewRenderer`, not here.

#pragma once

#include "../animation/SkinVertex.hpp" // ModelVertex (48-byte bind-pose vertex)

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// ─── Existing types ──────────────────────────────────────────────────────────

/// @brief Anti-aliasing mode selection — exposed to ImGui.
enum class AAMode : int
{
    Off = 0,      ///< No anti-aliasing.
    SMAA_1x = 1,  ///< Spatial SMAA only, zero ghosting.
    SMAA_T2x = 2, ///< 2-sample temporal + spatial SMAA (recommended).
};

/// @brief Live toggles for every render system — exposed to ImGui.
///
/// All default to true (everything on).  The renderer checks these each
/// frame and skips the corresponding pass/dispatch when disabled.
/// Graphics team should consult these inside each pass's entry point.
struct RenderToggles
{
    // Geometry passes
    bool pbrModels = true;       ///< Loaded scene models (opaque + transparent).
    bool entityModels = true;    ///< ECS-driven entity models (Renderable component).
    bool weaponViewmodel = true; ///< First-person weapon.
    bool skybox = true;          ///< Procedural / cubemap skybox.

    // Shadow
    bool shadows = true; ///< Shadow map pass + shadow sampling in PBR.

    // Post-processing
    bool ssao = true;        ///< Screen-space ambient occlusion.
    bool bloom = true;       ///< Bloom downsample + upsample chain.
    bool ssr = true;         ///< Screen-space reflections.
    bool volumetrics = true; ///< Volumetric lighting / god rays.
    bool tonemap = true;     ///< HDR → LDR tone mapping (disabling = raw HDR blit).

    // Effects
    bool particles = true; ///< GPU particle system.
    bool sdfText = true;   ///< SDF text rendering (HUD + world).

    // Skinned characters
    bool skinnedCharacters = true; ///< Instanced GPU skinning pass for player meshes.
};

/// @brief Per-entity render command — built by Game, consumed by Renderer::drawFrame.
///
/// One entry per visible static/dynamic entity (NOT skinned characters — those
/// go through `setSkinnedFrame`).  Game.cpp builds these from ECS each frame
/// and hands them off via `setEntityRenderList()`.
struct EntityRenderCmd
{
    int32_t modelIndex = -1;        ///< Renderer-side model handle (returned by `loadSceneModel` / `uploadSceneModel`).
    glm::mat4 worldTransform{1.0f}; ///< Full world transform (position × rotation × scale).
    glm::vec4 tint{1.0f};           ///< RGB multiplier into baseColorFactor (alpha unused).  Default = no tint.
};

/// @brief Dynamic point light — built by Game, injected into the PBR light array.
///
/// Game.cpp builds the list each frame from ECS (e.g. glow-emitting entities,
/// muzzle flashes) and hands it off via `setPointLights()`.  Renderer should
/// cap at some reasonable max (legacy used 6 dynamic + 2 reserved for sun/fill).
struct PointLight
{
    glm::vec3 position{0.0f}; ///< World-space position.
    glm::vec3 color{1.0f};    ///< Light colour (linear RGB).
    float intensity = 1.0f;   ///< Brightness multiplier.
    float range = 500.0f;     ///< Attenuation range (world units); falloff = 1 - (d²/r²).
};

/// @brief First-person weapon viewmodel descriptor sent per frame.
///
/// Game.cpp computes the viewmodel transform from camera state + sway/recoil
/// each frame and hands it off via `setWeaponViewmodel()`.
struct WeaponViewmodel
{
    int32_t modelIndex = -1;   ///< Renderer-side model handle.
    glm::mat4 transform{1.0f}; ///< Transform in viewmodel space (relative to camera).
    bool visible = false;      ///< False = skip drawing this frame (e.g. weapon hidden).
};

// ─── Skinned-character types (NEW) ───────────────────────────────────────────
//
// These three types form the interface between the animation system and the
// renderer's skinned-character pipeline.  See `NewRenderer::setSkinnedRig`
// and `NewRenderer::setSkinnedFrame` for usage.
//
// Pipeline:
//   1. Once at startup, Game.cpp builds `vector<RigMeshSource>` from
//      `CharacterRig::meshes()` (one entry per skinned mesh) and calls
//      `setSkinnedRig(meshes, numJoints)`.
//   2. Every frame, Game.cpp walks visible animated entities, builds two
//      parallel vectors (bone palette + per-instance metadata) and calls
//      `setSkinnedFrame(palette, instances)`.

/// @brief Per-vertex bone-influence record — 4 bones per vertex, weighted.
///
/// Lives parallel to the bind-pose vertex buffer.  Uploaded once at rig
/// install time as a second vertex buffer; the shader reads `boneIndices`
/// and `boneWeights` as vertex attributes (locations 3 and 4 in the
/// suggested layout — but graphics team owns the final shader, see
/// `setSkinnedRig` docs for the recommended attribute layout).
///
/// Layout (32 bytes, no padding) matches what the legacy `pbr_skinned.vert`
/// expected.  Keep it 32 bytes — changes here require shader updates.
struct BoneInfluence
{
    int boneIndices[4] = {0, 0, 0, 0};         ///< Joint indices into the bone palette.
    float boneWeights[4] = {0.0f, 0.0f, 0.0f, 0.0f}; ///< Skinning weights (should sum to ~1.0).
};

/// @brief Per-frame instance entry — one per visible animated character.
///
/// Game.cpp appends one of these to a `std::vector<SkinnedInstance>` every
/// frame for each visible character, sets `paletteBase` to `slot * numJoints`,
/// and passes the vector to `setSkinnedFrame()`.  The renderer uploads it as
/// an SSBO; the shader reads it as `instances[gl_InstanceIndex]`.
///
/// Layout (96 bytes) — keep std140-friendly.  Pads added so total is a
/// multiple of 16 and `tint` lands at byte offset 80.
struct SkinnedInstance
{
    glm::mat4 worldTransform{1.0f};         ///< Rig-local → world.  Composed CPU-side as T * R * S.
    uint32_t paletteBase = 0;               ///< First joint slot in the palette = instanceIndex * numJoints.
    uint32_t materialId = 0;                ///< Reserved; ignore for now (will index a material table later).
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 0.0f}; ///< rgb = per-player color, a = blend factor (0 = no tint).
};

/// @brief Source data for one mesh of a skinned character rig.
///
/// Game.cpp constructs a `std::vector<RigMeshSource>` from
/// `CharacterRig::meshes()` once (at startup, after the rig FBX loads)
/// and passes it to `setSkinnedRig()`.  The renderer copies everything
/// it needs to GPU buffers in that one call and never touches the
/// CPU-side data afterwards.
///
/// All three vectors describe the same triangle mesh in bind pose:
///   - `bindPoseVertices` carries position/normal/uv/tangent.
///   - `boneInfluences` is parallel: `boneInfluences[i]` belongs to
///     `bindPoseVertices[i]` (same length, same order).
///   - `indices` is a 32-bit triangle list (every 3 indices = 1 triangle).
struct RigMeshSource
{
    std::vector<ModelVertex> bindPoseVertices;   ///< Bind-pose vertices (never deformed CPU-side).
    std::vector<BoneInfluence> boneInfluences;   ///< Parallel to `bindPoseVertices`.  Must be same size.
    std::vector<uint32_t> indices;               ///< Triangle list (3 indices per triangle).
};
