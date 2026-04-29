/// @file RendererTypes.hpp
/// @brief Shared data types used across renderer implementations.
///
/// Extracted so that `IRenderer.hpp` and the concrete renderer headers can
/// depend on these structs without creating circular includes.

#pragma once

#include <glm/glm.hpp>

/// @brief Anti-aliasing mode selection -- exposed to ImGui.
enum class AAMode : int
{
    Off = 0,      ///< No anti-aliasing.
    SMAA_1x = 1,  ///< Spatial SMAA only, zero ghosting.
    SMAA_T2x = 2, ///< 2-sample temporal + spatial SMAA (recommended).
};

/// @brief Live toggles for every render system -- exposed to ImGui.
///
/// All default to true (everything on). The legacy renderer checks these each
/// frame and skips the corresponding pass/dispatch when disabled.
struct RenderToggles
{
    // Geometry passes
    bool pbrModels = true;       ///< Assimp-loaded scene models (opaque + transparent).
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
    bool tonemap = true;     ///< HDR -> LDR tone mapping (disabling = raw HDR blit).

    // Effects
    bool particles = true; ///< GPU particle system.
    bool sdfText = true;   ///< SDF text rendering (HUD + world).
};

/// @brief Per-entity render command -- built by Game, consumed by Renderer::drawFrame.
struct EntityRenderCmd
{
    int32_t modelIndex = -1;        ///< Index into Renderer::models[].
    glm::mat4 worldTransform{1.0f}; ///< Full world transform (position × rotation × scale).
};

/// @brief Dynamic point light -- built by Game, injected into the PBR light array.
struct PointLight
{
    glm::vec3 position{0.0f}; ///< World-space position.
    glm::vec3 color{1.0f};    ///< Light colour (linear RGB).
    float intensity = 1.0f;   ///< Brightness multiplier (passed as color.a in the UBO).
    float range = 500.0f;     ///< Attenuation range (world units); falloff = 1 - (d²/r²).
};

/// @brief First-person weapon viewmodel descriptor sent per frame.
struct WeaponViewmodel
{
    int32_t modelIndex = -1;   ///< Index into Renderer::models[].
    glm::mat4 transform{1.0f}; ///< Transform in viewmodel space (relative to camera).
    bool visible = false;
};
