/// @file AssetCatalog.hpp
/// @brief Static asset loading and render-default metadata.

#pragma once

#include "ecs/AssetRegistry.hpp"

#include <array>
#include <glm/vec3.hpp>

/// @brief Header-authored defaults for assets registered with AssetRegistry.
struct AssetDefinition
{
    const char* name = "";
    const char* filename = "";
    AssetRole role = AssetRole::Entity;
    glm::vec3 loadTranslation{0.0f};       ///< World translation used when loading static scene assets.
    float loadScale = 1.0f;                ///< Scale used when loading/uploading the model.
    bool flipUVs = false;                  ///< True for assets whose source UV orientation needs flipping.
    bool decomposeCollision = false;       ///< True to run V-HACD on non-convex collision sub-meshes.
    glm::vec3 renderScale{1.0f};           ///< Default Renderable scale.
    glm::vec3 renderTranslation{0.0f};     ///< Default Renderable local translation.
    glm::vec3 renderRotationDegrees{0.0f}; ///< Default Renderable local rotation in degrees.
};

inline const AssetDefinition kMapAsset{
    .name = "map1",
    .filename = "maps/map1.glb",
    .role = AssetRole::Map,
    .loadScale = 39.3701f,
};

// Porsche removed — its 75-mesh hierarchy floods the collision debug UI.
inline const std::array<AssetDefinition, 2> kPropAssets{{
    {.name = "pallet",
     .filename = "metallic_pallet_factory_store.glb",
     .role = AssetRole::Prop,
     .loadTranslation = {0.0f, 0.0f, 600.0f},
     .loadScale = 0.25f,
     .flipUVs = true,
     .decomposeCollision = true},
    {.name = "bottle",
     .filename = "bottle_a.glb",
     .role = AssetRole::Prop,
     .loadTranslation = {100.0f, 0.0f, 400.0f},
     .loadScale = 20.0f,
     .decomposeCollision = true},
}};

inline const AssetDefinition kWraithAsset{
    .name = "wraith",
    .filename = "Apex_Legend_Wraith.glb",
    .role = AssetRole::Entity,
    .loadScale = 8.0f,
};

inline const std::array<AssetDefinition, 4> kWeaponAssets{{
    {.name = "weapon_rifle",
     .filename = "r-301_-_apex_legends.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {0.025f, 0.025f, 0.025f}},
    {.name = "weapon_rocket",
     .filename = "r-301_-_apex_legends.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {0.025f, 0.025f, 0.025f}},
    {.name = "weapon_railgun",
     .filename = "apex_legends_triple_take_marksman.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {1.0f, 1.0f, 1.0f},
     .renderRotationDegrees = {90.0f, 0.0f, 0.0f}},
    {.name = "weapon_energy",
     .filename = "apex_legends_wingman_pistol.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {1.0f, 1.0f, 1.0f},
     .renderRotationDegrees = {90.0f, 0.0f, 0.0f}},
}};

inline const std::array<AssetDefinition, 3> kEffectAssets{{
    {.name = "glow_sphere", .role = AssetRole::Effect},
    {.name = "glow_sphere_movable", .role = AssetRole::Effect},
    {.name = "glow_cylinder", .role = AssetRole::Effect},
}};
