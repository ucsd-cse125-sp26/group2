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

/// @brief Mid-level character stance the right-hand anchor varies on. Matches
/// CharacterAnimator's internal `Mode` enum 1:1 except `HoldPose` /
/// `DebugOverride` (which both fall back to Locomotion). Authoring one anchor
/// per stance lets crouch / slide / wallrun have visibly different gun
/// positions — a crouched character pulls the gun in tighter, a sliding
/// character punches it forward, etc.
enum class HoldStance : std::uint8_t
{
    Locomotion = 0,
    Crouch,
    Airborne,
    Slide,
    WallRun,
    Count,
};

inline constexpr std::size_t kHoldStanceCount = static_cast<std::size_t>(HoldStance::Count);
inline constexpr std::array<const char*, kHoldStanceCount> kHoldStanceNames{
    "Locomotion", "Crouch", "Airborne", "Slide", "WallRun"};

/// @brief A single (position, rotation) anchor for the right-hand weapon hold.
///
/// `offset` is in the parent bone's (Spine2's) local frame; `rotation` is a
/// quaternion in that same frame. Stored as a quat — not Euler — to keep the
/// editor free of gimbal-lock surprises near pitch ≈ ±90°.
struct HoldAnchor
{
    glm::vec3 offset{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

/// @brief Third-person weapon attachment params.
///
/// The weapon mesh is parented to the right-hand bone after IK runs, so its
/// world placement is fully determined by:
///   1. `rightHandHolds[stance]` — where the wrist sits (Spine2-local).
///   2. `WeaponHandMountParams::rightHand.palm` — where the weapon meets the wrist (weapon-local).
/// There is no longer a separate "weapon hand offset" or "aim pivot" or
/// per-weapon yaw/pitch/roll because none of them participated in the bone-
/// parented derivation — they only fed the legacy fallback path that the
/// animated-character pipeline overrode.
struct ThirdPersonWeaponParams
{
    float scale = 1.0f;
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
    /// Right-hand weapon-hold anchor per stance. The animator pulls the right
    /// hand to `Spine2World × rightHandHolds[stance].offset` via IK each
    /// frame; the weapon mesh — parented to the right-hand bone — follows.
    /// Different stances can hold the weapon differently (e.g. crouched vs
    /// sliding); the candidate populator picks the entry matching the
    /// entity's current animator mode.
    std::array<HoldAnchor, kHoldStanceCount> rightHandHolds{};
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

/// @brief Palm plus per-finger targets for one hand on a weapon.
struct HandMountSet
{
    glm::vec3 elbowOffset{0.0f};
    HandMountPoint palm;                                         ///< Weapon-local hard anchor for the hand.
    std::array<HandMountPoint, kHandFingerMountCount> fingers{}; ///< Palm-local child offsets.
};

/// @brief Per-weapon hand mount targets used by third-person player arm/finger IK.
struct WeaponHandMountParams
{
    HandMountSet rightHand;
    HandMountSet leftHand;
    float viewmodelHandScale = 45.0f;
};

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
    // Per-stance offset deltas, applied additively on top of the weapon's
    // Locomotion anchor. Same values for every weapon for now — visually
    // sensible defaults that the user can override per-weapon via the
    // tweaker UI. Crouched: gun pulled in tight and slightly up.
    // Airborne: slightly forward (running/jumping leans into the weapon).
    // Slide: punched forward and down (gun aimed past sliding feet).
    // WallRun: pulled in tight (silhouette stays narrow against the wall).
    const HoldAnchor k_idleAnchor{.offset = {6.0f, 6.0f, 14.0f}, .rotation = glm::quat(1, 0, 0, 0)};
    const HoldAnchor k_crouchAnchor{.offset = {4.0f, 8.0f, 12.0f}, .rotation = glm::quat(1, 0, 0, 0)};
    const HoldAnchor k_airborneAnchor{.offset = {6.0f, 4.0f, 16.0f}, .rotation = glm::quat(1, 0, 0, 0)};
    const HoldAnchor k_slideAnchor{.offset = {6.0f, 2.0f, 18.0f}, .rotation = glm::quat(1, 0, 0, 0)};
    const HoldAnchor k_wallRunAnchor{.offset = {3.0f, 6.0f, 10.0f}, .rotation = glm::quat(1, 0, 0, 0)};
    const std::array<HoldAnchor, kHoldStanceCount> k_defaultHolds{
        k_idleAnchor, k_crouchAnchor, k_airborneAnchor, k_slideAnchor, k_wallRunAnchor};

    static const std::array<ThirdPersonWeaponParams, 4> k_params{{
        // Rifle — middleweight, full spine bend, moderate recoil.
        // Locomotion anchor hand-tuned via the 3P Weapon Tweaker; other
        // stances stay on the generic defaults until they're tuned.
        {.scale = 10.0f,
         .spineBendMultiplier = 1.0f,
         .hipLeanMultiplier = 0.1f,
         .recoilKickRad = 0.05f,
         .rightHandHolds = {{
             // Locomotion: hand-tuned.
             {.offset = {-12.69f, -8.23f, 22.32f},
              .rotation = glm::quat(0.4410f, 0.5464f, 0.4894f, 0.5171f)},
             k_crouchAnchor,
             k_airborneAnchor,
             k_slideAnchor,
             k_wallRunAnchor,
         }}},
        // Rocket launcher — heavy, slower upper-body response, big kick.
        {.scale = 0.025f,
         .spineBendMultiplier = 0.65f,
         .hipLeanMultiplier = 0.06f,
         .recoilKickRad = 0.18f,
         .rightHandHolds = {{
             {.offset = {7.0f, 8.0f, 18.0f}, .rotation = glm::quat(1, 0, 0, 0)},
             {.offset = {5.0f, 10.0f, 16.0f}, .rotation = glm::quat(1, 0, 0, 0)},
             {.offset = {7.0f, 6.0f, 20.0f}, .rotation = glm::quat(1, 0, 0, 0)},
             {.offset = {7.0f, 4.0f, 22.0f}, .rotation = glm::quat(1, 0, 0, 0)},
             {.offset = {4.0f, 8.0f, 14.0f}, .rotation = glm::quat(1, 0, 0, 0)},
         }}},
        // RailGun / charge rifle — heavy precision rifle, slower bend than the
        // assault rifle, similar kick to a rifle since the energy delivery is smooth.
        {.scale = 7.0f,
         .spineBendMultiplier = 0.85f,
         .hipLeanMultiplier = 0.08f,
         .recoilKickRad = 0.07f,
         .rightHandHolds = k_defaultHolds},
        // EnergyGun — light pistol, fast spine bend, gentle kick.
        {.scale = 1.0f,
         .spineBendMultiplier = 1.0f,
         .hipLeanMultiplier = 0.1f,
         .recoilKickRad = 0.03f,
         .rightHandHolds = {{
             {.offset = {5.0f, 4.0f, 12.0f}, .rotation = glm::quat(1, 0, 0, 0)},
             {.offset = {3.0f, 6.0f, 10.0f}, .rotation = glm::quat(1, 0, 0, 0)},
             {.offset = {5.0f, 2.0f, 14.0f}, .rotation = glm::quat(1, 0, 0, 0)},
             {.offset = {5.0f, 0.0f, 16.0f}, .rotation = glm::quat(1, 0, 0, 0)},
             {.offset = {3.0f, 4.0f, 8.0f}, .rotation = glm::quat(1, 0, 0, 0)},
         }}},
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns hand grip/mount points for a weapon type.
inline const WeaponHandMountParams& getWeaponHandMountParams(WeaponType type)
{
    static const std::array<WeaponHandMountParams, 4> k_params{{
        // Rifle: trigger hand near the rear grip, support hand wrapped around the mag well.
        {.rightHand = {.elbowOffset = {-8.4f, -17.99f, -20.35f},
                       .palm = {.offset = {-1.05f, -11.39f, -7.2f}, .rotationDegrees = {-6.0f, 92.0f, 94.0f}},
                       .fingers = {{
                           {.offset = {-0.3f, 3.05f, 1.9f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-2.6f, 0.3f, -2.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-2.7f, -0.5f, -0.4f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-2.6f, -1.0f, 0.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-2.5f, -1.3f, 1.4f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                       }}},
         .leftHand = {.elbowOffset = {13.7f, -13.14f, -13.3f},
                      .palm = {.offset = {3.6f, -12.99f, 2.0f}, .rotationDegrees = {-3.0f, -102.0f, -72.0f}},
                      .fingers = {{
                          {.offset = {2.3f, 0.7f, 1.3f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {2.6f, -0.2f, -1.9f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {2.7f, -0.8f, -0.6f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {2.6f, -1.1f, 0.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {2.5f, -1.2f, 1.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      }}},
         .viewmodelHandScale = 45.0f},
        // Rocket launcher: wider support stance.
        {.rightHand = {.elbowOffset = {14.0f, -11.0f, -25.0f},
                       .palm = {.offset = {3.0f, -5.5f, -12.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                       .fingers = {{
                           {.offset = {-2.0f, 1.5f, 2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-1.5f, 2.3f, 3.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-0.4f, 2.0f, 3.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {0.6f, 1.7f, 2.4f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {1.6f, 1.4f, 1.9f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                       }}},
         .leftHand = {.elbowOffset = {-22.0f, -10.0f, 6.0f},
                      .palm = {.offset = {-10.0f, -5.0f, 18.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      .fingers = {{
                          {.offset = {2.5f, 1.2f, -3.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {-0.8f, 2.2f, 2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {0.0f, 2.0f, 3.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {0.8f, 1.7f, 3.1f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {1.6f, 1.2f, 2.4f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      }}},
         .viewmodelHandScale = 45.0f},
        // Railgun: long front support grip.
        {.rightHand = {.elbowOffset = {12.0f, -9.0f, -20.0f},
                       .palm = {.offset = {2.5f, -4.5f, -8.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                       .fingers = {{
                           {.offset = {-2.0f, 1.3f, 2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-1.5f, 2.0f, 3.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-0.5f, 1.8f, 3.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {0.5f, 1.5f, 2.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {1.5f, 1.2f, 2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                       }}},
         .leftHand = {.elbowOffset = {-20.0f, -10.0f, 5.0f},
                      .palm = {.offset = {-8.0f, -4.5f, 16.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      .fingers = {{
                          {.offset = {2.2f, 1.3f, -2.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {-0.6f, 2.1f, 2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {0.0f, 1.9f, 3.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {0.7f, 1.5f, 3.2f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {1.4f, 1.1f, 2.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      }}},
         .viewmodelHandScale = 42.0f},
        // Energy gun / pistol: compact two-hand grip.
        {.rightHand = {.elbowOffset = {10.0f, -8.0f, -14.0f},
                       .palm = {.offset = {2.0f, -4.0f, -4.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                       .fingers = {{
                           {.offset = {-1.7f, 1.3f, 1.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-1.2f, 2.0f, 2.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {-0.2f, 1.7f, 2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {0.8f, 1.4f, 1.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                           {.offset = {1.7f, 1.1f, 1.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                       }}},
         .leftHand = {.elbowOffset = {-15.0f, -9.0f, 0.0f},
                      .palm = {.offset = {-5.5f, -4.5f, 5.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      .fingers = {{
                          {.offset = {2.0f, 1.3f, -2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {-0.7f, 2.2f, 1.8f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {0.0f, 2.0f, 2.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {0.7f, 1.7f, 2.6f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {1.3f, 1.3f, 2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      }}},
         .viewmodelHandScale = 42.0f},
    }};

    return k_params[static_cast<std::size_t>(type)];
}

/// @brief Returns first-person-only arm controls for a weapon type.
inline const FirstPersonHandMountParams& getFirstPersonHandMountParams(WeaponType type)
{
    static const std::array<FirstPersonHandMountParams, 4> k_params{{
        // Rifle
        {.rightArm = {.shoulderOffset = {20.0f, -22.0f, -38.0f},
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
         .scale = 45.0f},
        // Rocket
        {.rightArm = {.shoulderOffset = {22.0f, -24.0f, -44.0f},
                      .elbowOffset = {15.0f, -14.0f, -28.0f},
                      .palm = {.offset = {3.0f, -5.5f, -12.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      .fingers = {{
                          {.offset = {1.0f, -4.0f, -10.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {1.5f, -3.2f, -8.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {2.6f, -3.5f, -9.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {3.6f, -3.8f, -9.6f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {4.6f, -4.1f, -10.1f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      }}},
         .leftArm = {.shoulderOffset = {-30.0f, -24.0f, -20.0f},
                     .elbowOffset = {-23.0f, -14.0f, 4.0f},
                     .palm = {.offset = {-10.0f, -5.0f, 18.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                     .fingers = {{
                         {.offset = {-7.5f, -3.8f, 15.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-10.8f, -2.8f, 20.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-10.0f, -3.0f, 21.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-9.2f, -3.3f, 21.1f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-8.4f, -3.8f, 20.4f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                     }}},
         .scale = 45.0f},
        // RailGun / charge rifle
        {.rightArm = {.shoulderOffset = {20.0f, -22.0f, -40.0f},
                      .elbowOffset = {14.0f, -12.0f, -24.0f},
                      .palm = {.offset = {2.5f, -4.5f, -8.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      .fingers = {{
                          {.offset = {0.5f, -3.2f, -6.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {1.0f, -2.5f, -4.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {2.0f, -2.7f, -5.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {3.0f, -3.0f, -5.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {4.0f, -3.3f, -6.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      }}},
         .leftArm = {.shoulderOffset = {-30.0f, -23.0f, -20.0f},
                     .elbowOffset = {-22.0f, -13.0f, 2.0f},
                     .palm = {.offset = {-8.0f, -4.5f, 16.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                     .fingers = {{
                         {.offset = {-5.8f, -3.2f, 13.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-8.6f, -2.4f, 18.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-8.0f, -2.6f, 19.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-7.3f, -3.0f, 19.2f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-6.6f, -3.4f, 18.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                     }}},
         .scale = 42.0f},
        // EnergyGun
        {.rightArm = {.shoulderOffset = {18.0f, -22.0f, -34.0f},
                      .elbowOffset = {12.0f, -12.0f, -18.0f},
                      .palm = {.offset = {2.0f, -4.0f, -4.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      .fingers = {{
                          {.offset = {0.3f, -2.7f, -2.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {0.8f, -2.0f, -1.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {1.8f, -2.3f, -2.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {2.8f, -2.6f, -2.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                          {.offset = {3.7f, -2.9f, -3.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                      }}},
         .leftArm = {.shoulderOffset = {-25.0f, -22.0f, -18.0f},
                     .elbowOffset = {-17.0f, -12.0f, -2.0f},
                     .palm = {.offset = {-5.5f, -4.5f, 5.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                     .fingers = {{
                         {.offset = {-3.5f, -3.2f, 3.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-6.2f, -2.3f, 6.8f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-5.5f, -2.5f, 7.5f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-4.8f, -2.8f, 7.6f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                         {.offset = {-4.2f, -3.2f, 7.0f}, .rotationDegrees = {0.0f, 0.0f, 0.0f}},
                     }}},
         .scale = 42.0f},
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
