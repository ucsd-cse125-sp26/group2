/// @file ViewmodelConfig.hpp
/// @brief Per-weapon viewmodel, third-person, and recoil tuning parameters (header-only).

#pragma once

#include "ecs/components/Projectile.hpp"

#include <array>
#include <cstddef>
#include <glm/vec3.hpp>

/// @brief First-person viewmodel positioning/rotation params per weapon type.
struct ViewmodelParams
{
    float scale;
    float forward, right, down;
    float yawOffset, pitchOffset, rollOffset; // degrees
};

/// @brief Third-person weapon attachment params for remote players.
struct ThirdPersonWeaponParams
{
    float scale;
    glm::vec3 handOffset;                     // relative to player center (right, up, forward)
    float yawOffset, pitchOffset, rollOffset; // degrees
};

/// @brief Asset filename + load flags for a weapon model.
struct WeaponModelInfo
{
    const char* filename;
    bool flipUVs;
};

/// @brief Visual recoil params per weapon type (viewmodel-only, does not affect aim).
struct RecoilParams
{
    float pitchKick = 3.0f;      ///< Degrees of upward pitch kick per shot.
    float pushBack = 2.0f;       ///< Quake units of backward push per shot.
    float rollKick = 1.0f;       ///< Degrees of random roll kick per shot.
    float recoverySpeed = 12.0f; ///< Spring decay rate (higher = snappier recovery).
};

/// @brief Returns viewmodel positioning params for a weapon type.
inline const ViewmodelParams& getViewmodelParams(WeaponType type)
{
    static constexpr std::array<ViewmodelParams, 4> k_params{{
        // Rifle (R-301) — existing tuning
        {.scale = 0.03f,
         .forward = 21.0f,
         .right = 5.5f,
         .down = 22.5f,
         .yawOffset = 58.0f,
         .pitchOffset = 12.0f,
         .rollOffset = 2.0f},
        // Rocket — fallback to R-301 tuning
        {.scale = 0.03f,
         .forward = 21.0f,
         .right = 5.5f,
         .down = 22.5f,
         .yawOffset = 58.0f,
         .pitchOffset = 12.0f,
         .rollOffset = 2.0f},
        // RailGun (Triple Take) — marksman
        {.scale = 1.5f,
         .forward = 38.0f,
         .right = 15.0f,
         .down = 16.5f,
         .yawOffset = -9.0f,
         .pitchOffset = 93.0f,
         .rollOffset = -8.0f},
        // EnergyGun (Wingman)
        {.scale = 1.5f,
         .forward = 23.5f,
         .right = 11.5f,
         .down = 12.0f,
         .yawOffset = 2.0f,
         .pitchOffset = 90.0f,
         .rollOffset = 4.5f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns third-person weapon attachment params for remote players.
inline const ThirdPersonWeaponParams& getThirdPersonWeaponParams(WeaponType type)
{
    static const std::array<ThirdPersonWeaponParams, 4> k_params{{
        // Rifle (R-301)
        {.scale = 0.025f,
         .handOffset = {1.5f, -3.5f, 14.0f},
         .yawOffset = -47.0f,
         .pitchOffset = 13.0f,
         .rollOffset = 0.0f},
        // Rocket — fallback to R-301
        {.scale = 0.025f,
         .handOffset = {1.5f, -3.5f, 14.0f},
         .yawOffset = -47.0f,
         .pitchOffset = 13.0f,
         .rollOffset = 0.0f},
        // RailGun (Triple Take)
        {.scale = 1.0f, .handOffset = {7.5f, 7.5f, 15.0f}, .yawOffset = 0.0f, .pitchOffset = 96.0f, .rollOffset = 2.0f},
        // EnergyGun (Wingman)
        {.scale = 1.0f, .handOffset = {7.5f, 7.0f, 6.0f}, .yawOffset = 6.0f, .pitchOffset = 94.0f, .rollOffset = 0.0f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns the GLB filename and load flags for a weapon type.
inline WeaponModelInfo getWeaponModelInfo(WeaponType type)
{
    static constexpr std::array<WeaponModelInfo, 4> k_infos{{
        {.filename = "r-301_-_apex_legends.glb", .flipUVs = true},              // Rifle
        {.filename = "r-301_-_apex_legends.glb", .flipUVs = true},              // Rocket (fallback)
        {.filename = "apex_legends_triple_take_marksman.glb", .flipUVs = true}, // RailGun
        {.filename = "apex_legends_wingman_pistol.glb", .flipUVs = true},       // EnergyGun
    }};
    return k_infos[static_cast<std::size_t>(type)];
}

/// @brief Returns visual recoil params for a weapon type.
inline const RecoilParams& getRecoilParams(WeaponType type)
{
    static constexpr std::array<RecoilParams, 4> k_params{{
        // Rifle (R-301) — full-auto, low per-shot, fast recovery
        {.pitchKick = 2.0f, .pushBack = 1.5f, .rollKick = 0.5f, .recoverySpeed = 14.0f},
        // Rocket — big boom
        {.pitchKick = 8.0f, .pushBack = 5.0f, .rollKick = 2.0f, .recoverySpeed = 6.0f},
        // RailGun (Triple Take) — medium
        {.pitchKick = 5.0f, .pushBack = 3.0f, .rollKick = 1.0f, .recoverySpeed = 8.0f},
        // EnergyGun (Wingman) — hand cannon
        {.pitchKick = 6.0f, .pushBack = 4.0f, .rollKick = 1.5f, .recoverySpeed = 7.0f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}
