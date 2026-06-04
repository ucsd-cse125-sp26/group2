/// @file AssetCatalog.hpp
/// @brief Static asset loading and render-default metadata.

#pragma once

#include "ecs/AssetRegistry.hpp"
#include "ecs/components/WeaponState.hpp"

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

inline const std::array<AssetDefinition, 0> kPropAssets{};

inline const AssetDefinition kRocketProjectile{
    .name = "rocket_projectile",
    .filename = "rocket_projectile.glb", // update with rocket projectile asset
    .role = AssetRole::Entity,
    .loadScale = 20.0f,
    .flipUVs = true,
};

inline const AssetDefinition kGrenadeModel{
    .name = "grenade",
    .filename = "grenade.glb",
    .role = AssetRole::Entity,
    .flipUVs = true,
    .renderScale = {20.0f, 20.0f, 20.0f},
};

inline const AssetDefinition kHEGrenadeModel{
    .name = "he_grenade",
    .filename = "grenade_he.glb",
    .role = AssetRole::Entity,
    .flipUVs = true,
    .renderScale = {9.0f, 9.0f, 9.0f},
};

inline const AssetDefinition kStickyGrenadeModel{
    .name = "sticky_grenade",
    .filename = "grenade_sticky.glb",
    .role = AssetRole::Entity,
    .flipUVs = true,
    .renderScale = {0.4f, 0.4f, 0.4f},
};

inline const AssetDefinition kMolotovModel{
    .name = "molotov",
    .filename = "grenade_fire.glb",
    .role = AssetRole::Entity,
    .flipUVs = true,
    .renderScale = {600.0f, 600.0f, 600.0f},
};

inline const AssetDefinition kMedkitModel{
    .name = "medkit",
    .filename = "medkit.glb",
    .role = AssetRole::Entity,
    .flipUVs = true,
    .renderScale = {20.0f, 20.0f, 20.0f},
};

inline const std::array<AssetDefinition, kRenderableWeaponTypeCount> kWeaponAssets{{
    {.name = "weapon_rifle",
     .filename = "assault_rifle.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {20.0f, 20.0f, 20.0f}},
    {.name = "weapon_rocket",
     .filename = "rocket_launcher.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {200.0f, 200.0f, 200.0f}},
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
    {.name = "weapon_shotgun",
     .filename = "energy_gun.glb", // reuses energy gun mesh until a dedicated shotgun model is authored.
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

/// @brief Per-weapon animated first-person viewmodel assets.
///
/// A weapon with a non-empty `viewmodelGlb` uses the animated viewmodel pipeline:
/// a textured, skinned gun rig with baked first-person clips (draw/reload),
/// rendered in first person (gun + `armsGlb` hands). A weapon with an empty
/// `viewmodelGlb` falls back to the legacy static viewmodel mesh.
///
/// To add a weapon: run `tools/convert_weapon_viewmodel.py` on its Apex `_v`
/// cast + textures + first-person clips to produce `apex_<weapon>.glb` +
/// `apex_<weapon>_arms.glb`, then fill in the row here.
struct WeaponViewmodelAssets
{
    const char* viewmodelGlb = ""; ///< Skinned gun GLB (textured, baked clips). Empty -> static fallback.
    const char* armsGlb = "";      ///< First-person arms GLB (same baked clips). Empty -> no 1P hands.
    bool flipUVs = false;          ///< Source UV orientation flip at load (skinned_geometry_textured.frag flips V).
};

inline const std::array<WeaponViewmodelAssets, kRenderableWeaponTypeCount> kWeaponViewmodelAssets{{
    // Rifle (R-301) — animated first-person viewmodel. flipUVs=true: the lit
    // geometry_shadowed.frag samples V un-flipped (like static models), so the
    // glTF→DCC V convention is corrected at import (matches the world model path).
    {.viewmodelGlb = "apex_r301.glb", .armsGlb = "apex_r301_arms.glb", .flipUVs = true},
    // Rocket — static fallback.
    {},
    // RailGun — static fallback (kept on the legacy viewmodel for now).
    {},
    // EnergyGun — static fallback.
    {},
    // Shotgun — static fallback.
    {},
}};
