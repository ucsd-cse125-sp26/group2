/// @file ViewmodelConfig.hpp
/// @brief Per-weapon viewmodel, third-person, and recoil tuning parameters (header-only).

#pragma once

#include "ecs/components/Projectile.hpp"
#include "ecs/components/WeaponHoldPose.hpp"

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

/// @brief Per-weapon procedural-overlay + scale params for the third-person body.
///
/// The weapon placement itself is now driven entirely by `WeaponHoldPose` (the
/// weapon is a rigid child of Spine2 and the arms are FK-posed). What remains
/// here is the body's procedural response — spine bend / hip lean / recoil kick
/// — plus the weapon mesh scale.
struct ThirdPersonWeaponParams
{
    /// Per-weapon-class scaler on the Phase F procedural spine bend (1.0 = full,
    /// heavier weapons get less so the character looks like it struggles with
    /// the weight when aiming up/down).
    float spineBendMultiplier = 1.0f;
    /// Per-weapon-class hip-lean coupling: pelvis pitch (radians) per radian of
    /// aim pitch, opposite sign. ~0.10 reads as a confident shooter's stance;
    /// 0 disables coupling entirely.
    float hipLeanMultiplier = 0.1f;
    /// Per-weapon-class recoil kick magnitude in radians, applied additively to
    /// the spine bend when the player fires a shot.
    float recoilKickRad = 0.06f;
};

/// @brief A weapon-local grip point for either hand.
///
/// `offset` is measured in render/world units after the weapon has been placed:
/// x = weapon right, y = weapon up, z = weapon forward.
struct HandMountPoint
{
    glm::vec3 offset{0.0f};
    glm::vec3 rotationDegrees{0.0f};
};

inline constexpr std::size_t kHandFingerMountCount = 5;
inline constexpr std::array<const char*, kHandFingerMountCount> kHandFingerMountNames{
    "Thumb", "Index", "Middle", "Ring", "Pinky"};

/// @brief First-person arm controls, independent from third-person weapon grips.
struct FirstPersonArmMountSet
{
    glm::vec3 shoulderOffset{0.0f};
    glm::vec3 elbowOffset{0.0f};
    HandMountPoint palm;                                         ///< Weapon-local hard anchor for the hand.
    std::array<HandMountPoint, kHandFingerMountCount> fingers{}; ///< Palm-local child offsets.
};

/// @brief Per-weapon first-person shoulder/elbow/palm/finger target data.
struct FirstPersonHandMountParams
{
    FirstPersonArmMountSet rightArm;
    FirstPersonArmMountSet leftArm;
    float scale = 45.0f;
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
    static constexpr std::array<ViewmodelParams, kRenderableWeaponTypeCount> k_params{{
        // Rifle — existing tuning
        {.scale = 39.0f,
         .forward = 60.0f,
         .right = 30.0f,
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
         .forward = 45.0f,
         .right = 27.0f,
         .down = 27.0f,
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f},
        // EnergyGun
        {.scale = 20.0f,
         .forward = 65.0f,
         .right = 35.0f,
         .down = 24.0f,
         .yawOffset = 0.0f,
         .pitchOffset = 0.0f,
         .rollOffset = 0.0f},
        // Shotgun — reuses EnergyGun viewmodel tuning (same mesh).
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

/// @brief Returns third-person procedural/scale params for a weapon type.
inline const ThirdPersonWeaponParams& getThirdPersonWeaponParams(WeaponType type)
{
    static const std::array<ThirdPersonWeaponParams, kRenderableWeaponTypeCount> k_params{{
        // Rifle — middleweight, full spine bend, moderate recoil.
        {.spineBendMultiplier = 1.0f, .hipLeanMultiplier = 0.1f, .recoilKickRad = 0.05f},
        // Rocket launcher — heavy, slower upper-body response, big kick.
        {.spineBendMultiplier = 0.65f, .hipLeanMultiplier = 0.06f, .recoilKickRad = 0.18f},
        // RailGun / charge rifle — heavy precision rifle, slower bend, rifle-ish kick.
        {.spineBendMultiplier = 0.85f, .hipLeanMultiplier = 0.08f, .recoilKickRad = 0.07f},
        // EnergyGun — light pistol, fast spine bend, gentle kick.
        {.spineBendMultiplier = 1.0f, .hipLeanMultiplier = 0.1f, .recoilKickRad = 0.03f},
        // Shotgun — copies EnergyGun (same mesh); heavier kick.
        {.spineBendMultiplier = 1.0f, .hipLeanMultiplier = 0.1f, .recoilKickRad = 0.12f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns the default third-person FK hold pose for a weapon type.
///
/// These are compile-time fallbacks; at runtime each weapon's pose is loaded
/// from `assets/weapons/<name>.hold.toml` if present and live-tuned via the
/// Weapon Hold tweaker (which saves back to that TOML).
///
/// Per-weapon hold poses hand-tuned in-game via the Weapon Hold tweaker
/// (spine-relative gun placement + scale + 3-DOF FK arm/finger angles), exported
/// from each weapon's `<name>.hold.toml`. Finger curl is shared across weapons;
/// the offset, scale, and arm bone angles differ per weapon. Runtime overrides
/// still come from `assets/weapons/<name>.hold.toml`.
inline const WeaponHoldPose& getWeaponHoldPose(WeaponType type)
{
    using BoneAngles = std::array<glm::vec3, kArmHoldBoneCount>;

    // Shared finger curl (right = trigger hand, left = support hand wrapping the
    // foregrip). (pitch, yaw, roll) degrees; layout matches GripPose::index.
    static const std::array<glm::vec3, kGripPoseJointCount> k_rightFingers{{
        {30.0f, 20.0f, 0.0f}, {35.0f, 0.0f, 0.0f}, {25.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, // thumb
        {25.0f, 0.0f, 0.0f}, {40.0f, 0.0f, 0.0f}, {35.0f, 0.0f, 0.0f}, {15.0f, 0.0f, 0.0f},  // index
        {55.0f, 0.0f, 0.0f}, {75.0f, 0.0f, 0.0f}, {70.0f, 0.0f, 0.0f}, {30.0f, 0.0f, 0.0f},  // middle
        {60.0f, 0.0f, 0.0f}, {80.0f, 0.0f, 0.0f}, {70.0f, 0.0f, 0.0f}, {30.0f, 0.0f, 0.0f},  // ring
        {60.0f, 0.0f, 0.0f}, {80.0f, 0.0f, 0.0f}, {70.0f, 0.0f, 0.0f}, {30.0f, 0.0f, 0.0f},  // pinky
    }};
    static const std::array<glm::vec3, kGripPoseJointCount> k_leftFingers{{
        {35.0f, -20.0f, 0.0f}, {40.0f, 0.0f, 0.0f}, {30.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, // thumb
        {65.0f, 0.0f, 0.0f}, {80.0f, 0.0f, 0.0f}, {70.0f, 0.0f, 0.0f}, {30.0f, 0.0f, 0.0f},   // index
        {70.0f, 0.0f, 0.0f}, {85.0f, 0.0f, 0.0f}, {70.0f, 0.0f, 0.0f}, {30.0f, 0.0f, 0.0f},   // middle
        {70.0f, 0.0f, 0.0f}, {85.0f, 0.0f, 0.0f}, {70.0f, 0.0f, 0.0f}, {30.0f, 0.0f, 0.0f},   // ring
        {65.0f, 0.0f, 0.0f}, {85.0f, 0.0f, 0.0f}, {70.0f, 0.0f, 0.0f}, {30.0f, 0.0f, 0.0f},   // pinky
    }};

    auto make = [&](glm::vec3 offset, float scale, BoneAngles right, BoneAngles left) {
        WeaponHoldPose p;
        p.spineOffset = offset;
        p.spineRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        p.scale = scale;
        p.rightArm.boneAngles = right;
        p.leftArm.boneAngles = left;
        p.rightArm.fingerAngles = k_rightFingers;
        p.leftArm.fingerAngles = k_leftFingers;
        return p;
    };

    static const std::array<WeaponHoldPose, kRenderableWeaponTypeCount> k_params{{
        // Rifle
        make({-21.0f, 23.5f, 66.25f}, 39.4f,
             {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 44.0f}, {-107.0f, -7.0f, 13.0f}, {-1.0f, 24.0f, -41.0f}}},
             {{{24.0f, 0.0f, 0.0f}, {36.0f, 0.0f, 48.0f}, {71.0f, -3.0f, 6.0f}, {-11.0f, -46.0f, -23.0f}}}),
        // Rocket launcher
        make({-21.0f, -28.25f, 28.75f}, 39.4f,
             {{{0.0f, 0.0f, 0.0f}, {0.0f, 42.0f, 18.0f}, {-155.0f, 4.0f, 9.0f}, {6.0f, 106.0f, -62.0f}}},
             {{{24.0f, 0.0f, 0.0f}, {40.0f, -16.0f, 46.0f}, {103.0f, -3.0f, 5.0f}, {-6.0f, -57.0f, -41.0f}}}),
        // RailGun / charge rifle
        make({-21.0f, -18.0f, 66.25f}, 39.4f,
             {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 44.0f}, {-107.0f, -7.0f, 13.0f}, {-1.0f, 24.0f, -41.0f}}},
             {{{24.0f, 0.0f, 0.0f}, {36.0f, 0.0f, 48.0f}, {71.0f, -3.0f, 6.0f}, {-11.0f, -46.0f, -23.0f}}}),
        // EnergyGun
        make({-21.0f, 3.25f, 80.0f}, 22.9f,
             {{{0.0f, -14.0f, 0.0f}, {0.0f, 0.0f, 35.0f}, {-120.0f, 5.0f, 7.0f}, {13.0f, 53.0f, -27.0f}}},
             {{{24.0f, 0.0f, 0.0f}, {36.0f, 0.0f, 48.0f}, {71.0f, -3.0f, 6.0f}, {-11.0f, -123.0f, -21.0f}}}),
        // Shotgun — reuses the EnergyGun model + pose.
        make({-21.0f, 3.25f, 80.0f}, 22.9f,
             {{{0.0f, -14.0f, 0.0f}, {0.0f, 0.0f, 35.0f}, {-120.0f, 5.0f, 7.0f}, {13.0f, 53.0f, -27.0f}}},
             {{{24.0f, 0.0f, 0.0f}, {36.0f, 0.0f, 48.0f}, {71.0f, -3.0f, 6.0f}, {-11.0f, -123.0f, -21.0f}}}),
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns first-person-only arm controls for a weapon type.
inline const FirstPersonHandMountParams& getFirstPersonHandMountParams(WeaponType type)
{
    // Rifle defaults are the only hand-tuned first-person arm set. Every
    // other weapon reuses them until per-weapon tuning lands.
    static constexpr FirstPersonHandMountParams k_rifleFirstPerson{
        .rightArm = {.shoulderOffset = {20.0f, -22.0f, -38.0f},
                     .elbowOffset = {14.0f, -12.0f, -22.0f},
                     .palm = {.offset = {2.5f, -4.0f, -7.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                     .fingers = {{
                         {.offset = {0.5f, -2.8f, -5.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {1.0f, -2.0f, -3.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {2.0f, -2.3f, -4.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {3.0f, -2.6f, -4.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {4.0f, -2.9f, -5.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                     }}},
        .leftArm = {.shoulderOffset = {-28.0f, -23.0f, -24.0f},
                    .elbowOffset = {-20.0f, -13.0f, -4.0f},
                    .palm = {.offset = {-8.0f, -4.5f, 13.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                    .fingers = {{
                        {.offset = {-6.0f, -3.2f, 10.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                        {.offset = {-8.5f, -2.2f, 15.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                        {.offset = {-8.0f, -2.4f, 16.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                        {.offset = {-7.2f, -2.8f, 16.2f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                        {.offset = {-6.5f, -3.2f, 15.6f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                    }}},
        .scale = 45.0f};
    static const std::array<FirstPersonHandMountParams, kRenderableWeaponTypeCount> k_params{{
        // Rifle
        k_rifleFirstPerson,
        // Rocket — rifle defaults until tuned.
        k_rifleFirstPerson,
        // RailGun / charge rifle — rifle defaults until tuned.
        k_rifleFirstPerson,
        // EnergyGun — rifle defaults until tuned.
        k_rifleFirstPerson,
        // Shotgun — rifle defaults until tuned.
        k_rifleFirstPerson,
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns world weapon pickup/spawner model params for a weapon type.
inline const WeaponSpawnerModelParams& getWeaponSpawnerModelParams(WeaponType type)
{
    static const WeaponSpawnerModelParams k_grenadeParams{
        .scale = {20.0f, 20.0f, 20.0f},
        .translation = {0.0f, 10.0f, 0.0f},
        .yawOffset = 0.0f,
        .pitchOffset = 0.0f,
        .rollOffset = 0.0f,
        .spinDegreesPerSecond = 45.0f,
        .bobAmplitude = 6.0f,
        .bobHz = 0.6f,
    };
    if (type == WeaponType::HEGrenade || type == WeaponType::Molotov || type == WeaponType::Sticky) {
        return k_grenadeParams;
    }

    static const std::array<WeaponSpawnerModelParams, kRenderableWeaponTypeCount> k_params{{
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
        // Shotgun — pickup model reuses energy gun mesh.
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
    static constexpr std::array<WeaponModelInfo, kRenderableWeaponTypeCount> k_infos{{
        {.filename = "assault_rifle.glb", .flipUVs = true},
        {.filename = "rocket_launcher.glb", .flipUVs = true},
        {.filename = "rail_gun.glb", .flipUVs = true},
        {.filename = "energy_gun.glb", .flipUVs = true},
        {.filename = "energy_gun.glb", .flipUVs = true}, // Shotgun — reuses energy gun model
    }};
    return k_infos[static_cast<std::size_t>(type)];
}

/// @brief Returns visual recoil params for a weapon type.
inline const RecoilParams& getRecoilParams(WeaponType type)
{
    static constexpr std::array<RecoilParams, kRenderableWeaponTypeCount> k_params{{
        // Rifle (R-301) — full-auto, low per-shot, fast recovery
        {.pitchKick = 2.0f, .pushBack = 1.5f, .rollKick = 0.5f, .recoverySpeed = 14.0f},
        // Rocket — big boom
        {.pitchKick = 8.0f, .pushBack = 5.0f, .rollKick = 2.0f, .recoverySpeed = 6.0f},
        // RailGun (Triple Take) — medium
        {.pitchKick = 5.0f, .pushBack = 3.0f, .rollKick = 1.0f, .recoverySpeed = 8.0f},
        // EnergyGun (Wingman) — hand cannon
        {.pitchKick = 6.0f, .pushBack = 4.0f, .rollKick = 1.5f, .recoverySpeed = 7.0f},
        // Shotgun (Peacekeeper) — heavy single thump
        {.pitchKick = 9.0f, .pushBack = 6.0f, .rollKick = 1.0f, .recoverySpeed = 5.0f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}
