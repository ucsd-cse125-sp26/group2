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

/// @brief World weapon spawner model params.
struct WeaponSpawnerModelParams
{
    glm::vec3 scale{1.0f};
    glm::vec3 translation{0.0f};
    float yawOffset = 0.0f;
    float pitchOffset = 0.0f;
    float rollOffset = 0.0f;
    float spinDegreesPerSecond = 45.0f;
    float bobAmplitude = 6.0f;
    float bobHz = 0.6f;
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
        // Rifle — existing tuning
        {.scale = 39.0f,
         .forward = 78.0f,
         .right = 37.0f,
         .down = -11.0f,
         .yawOffset = 0.0f,
         .pitchOffset = -1.0f,
         .rollOffset = 0.0f},
        // Rocket — fallback to rifle tuning
        {.scale = 39.0f,
         .forward = 7.0f,
         .right = 27.0f,
         .down = 77.5f,
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f},
        // RailGun — marksman
        {.scale = 20.0f,
         .forward = 48.0f,
         .right = 30.0f,
         .down = 27.0f,
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f},
        // EnergyGun
        {.scale = 20.0f,
         .forward = 80.0f,
         .right = 38.5f,
         .down = 24.0f,
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns third-person weapon attachment params for remote players.
inline const ThirdPersonWeaponParams& getThirdPersonWeaponParams(WeaponType type)
{
    static const std::array<ThirdPersonWeaponParams, 4> k_params{{
        // Rifle
        {.scale = 0.025f,
         .handOffset = {1.5f, -3.5f, 14.0f},
         .yawOffset = -47.0f,
         .pitchOffset = 13.0f,
         .rollOffset = 0.0f},
        // Rocket
        {.scale = 0.025f,
         .handOffset = {1.5f, -3.5f, 14.0f},
         .yawOffset = -47.0f,
         .pitchOffset = 13.0f,
         .rollOffset = 0.0f},
        // RailGun
        {.scale = 1.0f, .handOffset = {7.5f, 7.5f, 15.0f}, .yawOffset = 0.0f, .pitchOffset = 96.0f, .rollOffset = 2.0f},
        // EnergyGun
        {.scale = 1.0f, .handOffset = {7.5f, 7.0f, 6.0f}, .yawOffset = 6.0f, .pitchOffset = 94.0f, .rollOffset = 0.0f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns world weapon pickup/spawner model params for a weapon type.
inline const WeaponSpawnerModelParams& getWeaponSpawnerModelParams(WeaponType type)
{
    static const std::array<WeaponSpawnerModelParams, 4> k_params{{
        // Rifle
        {.scale = {16.0f, 16.0f, 16.0f},
         .translation = {0.0f, 16.0f, 0.0f},
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f,
         .spinDegreesPerSecond = 45.0f,
         .bobAmplitude = 6.0f,
         .bobHz = 0.6f},
        // Rocket
        {.scale = {15.0f, 15.0f, 15.0f},
         .translation = {0.0f, -25.0f, -4.0f},
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f,
         .spinDegreesPerSecond = 45.0f,
         .bobAmplitude = 6.0f,
         .bobHz = 0.6f},
        // RailGun
        {.scale = {15.0f, 15.0f, 15.0f},
         .translation = {0.0f, -5.0f, 0.0f},
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f,
         .spinDegreesPerSecond = 45.0f,
         .bobAmplitude = 6.0f,
         .bobHz = 0.6f},
        // EnergyGun
        {.scale = {10.0f, 10.0f, 10.0f},
         .translation = {0.0f, 2.0f, 0.0f},
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f,
         .spinDegreesPerSecond = 45.0f,
         .bobAmplitude = 6.0f,
         .bobHz = 0.6f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns the GLB filename and load flags for a weapon type.
inline WeaponModelInfo getWeaponModelInfo(WeaponType type)
{
    static constexpr std::array<WeaponModelInfo, 4> k_infos{{
        {.filename = "assault_rifle.glb", .flipUVs = true},
        {.filename = "rocket_launcher.glb", .flipUVs = true},
        {.filename = "rail_gun.glb", .flipUVs = true},
        {.filename = "energy_gun.glb", .flipUVs = true},
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
