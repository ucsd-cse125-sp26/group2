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
    bool loadCollision = true;             ///< False for visual-only comparison/debug props.
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

inline const std::array<AssetDefinition, 2> kPropAssets{{
    {.name = "suzanne_substance_usdz",
     .filename = "suzanne_substance.usdz",
     .role = AssetRole::Prop,
     .loadTranslation = {140.0f, 55.0f, 472.0f},
     .loadScale = 30.0f,
     .loadCollision = false},
    {.name = "suzanne_substance_glb",
     .filename = "suzanne_substance_v2.glb",
     .role = AssetRole::Prop,
     .loadTranslation = {255.0f, 55.0f, 472.0f},
     .loadScale = 30.0f,
     .loadCollision = false},
}};

inline const AssetDefinition kRocketProjectile{
    .name = "rocket_projectile",
    .filename = "rocket_projectile.glb", // update with rocket projectile asset
    .role = AssetRole::Entity,
    .loadScale = 20.0f,
    .flipUVs = true,
};

inline const std::array<AssetDefinition, 4> kWeaponAssets{{
    {.name = "weapon_rifle",
     .filename = "assault_rifle_with_mountpoints.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {20.0f, 20.0f, 20.0f}},
    {.name = "weapon_rocket",
     .filename = "rocket_launcher.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {20.0f, 20.0f, 20.0f}},
    {.name = "weapon_railgun",
     .filename = "rail_gun.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {20.0f, 20.0f, 20.0f},
     .renderRotationDegrees = {0.0f, 0.0f, 0.0f}},
    {.name = "weapon_energy",
     .filename = "energy_gun.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {20.0f, 20.0f, 20.0f},
     .renderRotationDegrees = {0.0f, 0.0f, 0.0f}},
}};

inline const std::array<AssetDefinition, 3> kEffectAssets{{
    {.name = "glow_sphere", .role = AssetRole::Effect},
    {.name = "glow_sphere_movable", .role = AssetRole::Effect},
    {.name = "glow_cylinder", .role = AssetRole::Effect},
}};
