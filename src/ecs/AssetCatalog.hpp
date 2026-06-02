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

inline const AssetDefinition kMedkitModel{
    .name = "medkit",
    .filename = "medkit.glb",
    .role = AssetRole::Entity,
    .flipUVs = true,
    .renderScale = {20.0f, 20.0f, 20.0f},
};

inline const std::array<AssetDefinition, kRenderableWeaponTypeCount> kWeaponAssets{{
    {.name = "weapon_rifle",
     .filename = "apex_r301.glb", // Apex R-301 (first-person viewmodel mesh); ~34u native, so ~1.0 scale.
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {1.0f, 1.0f, 1.0f}},
    {.name = "weapon_rocket",
     .filename = "rocket_launcher.glb",
     .role = AssetRole::Entity,
     .flipUVs = true,
     .renderScale = {200.0f, 200.0f, 200.0f}},
    {.name = "weapon_railgun",
     .filename = "kraber.glb", // Apex Kraber (replaces the charge rifle); same ~1.0 native scale as the R-301.
     .role = AssetRole::Entity,
     .flipUVs = false,
     .renderScale = {1.0f, 1.0f, 1.0f},
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

/// @brief Per-weapon animated viewmodel assets (the unified, R-301-style path).
///
/// A weapon with a non-empty `viewmodelGlb` uses the animated viewmodel pipeline:
/// a textured, skinned gun rig with baked first-person clips (idle/draw/reload),
/// rendered both in first person (gun + `armsGlb` hands) and — since the same
/// GLB carries the shared `ja_c_propGun` attach bone — mounted on the
/// third-person character. A weapon with an empty `viewmodelGlb` gracefully
/// falls back to the legacy static `kWeaponAssets[type].filename` mesh.
///
/// To add a weapon: run `tools/convert_weapon_viewmodel.py` on its Apex `_v`
/// cast + textures + first-person clips to produce `apex_<weapon>.glb` +
/// `apex_<weapon>_arms.glb`, then fill in the row here. No code changes, no
/// per-weapon offset tuning (the propGun bind places it exactly).
struct WeaponViewmodelAssets
{
    const char* viewmodelGlb = ""; ///< Skinned gun GLB (textured, ja_c_propGun, baked clips). 1P + 3P.
    const char* armsGlb = "";      ///< First-person arms GLB (same baked clips). Empty -> no 1P hands.
    bool flipUVs = true;           ///< Source UV orientation flip at load.
};

inline const std::array<WeaponViewmodelAssets, kRenderableWeaponTypeCount> kWeaponViewmodelAssets{{
    // Rifle (R-301) — fully built.
    {.viewmodelGlb = "apex_r301.glb", .armsGlb = "apex_r301_arms.glb", .flipUVs = true},
    // Rocket — static fallback until a viewmodel is authored.
    {},
    // RailGun — the Apex Charge Rifle ("defender" codename). Built via
    // tools/convert_weapon_viewmodel.py from its ptpov_defender _v model + the
    // real camera-space ptpov_defender clips (draw/reload), so it animates in
    // first-person like the R-301. (Replaces the Kraber, which had no first-person
    // viewmodel clips in the Apex data — only a 3rd-person pilot reload.)
    {.viewmodelGlb = "apex_chargerifle.glb", .armsGlb = "apex_chargerifle_arms.glb", .flipUVs = false},
    // EnergyGun — static fallback.
    {},
    // Shotgun — static fallback.
    {},
}};
