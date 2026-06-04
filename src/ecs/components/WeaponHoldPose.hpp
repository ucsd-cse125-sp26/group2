/// @file WeaponHoldPose.hpp
/// @brief Authored per-weapon third-person hold pose (FK weapon-hold rewrite).
///
/// Replaces the old analytical two-bone arm IK + Spine2 hold-anchor system.
/// The new model is dead simple and fully artist-/debug-tunable:
///   * The weapon is a rigid CHILD of the upper-spine bone (`mixamorig:Spine2`).
///     Its world transform is
///         entityWorld × Spine2World × translate(spineOffset) × mat4(spineRotation) × scale.
///   * The arms are pure forward kinematics. Each bone in the chain
///     Shoulder → UpperArm → ForeArm → Hand — plus every finger joint — is
///     posed by a single authored (pitch, yaw, roll) triple applied as a local
///     rotation relative to the bone's rest pose. No solving, no targets: the
///     hands (and therefore the whole arm) hang rigidly off the spine, so they
///     follow the spine bend / aim pitch automatically.
///
/// Three rotation DOF per bone — pitch about local Z, yaw about local Y, roll
/// (twist) about local X — give full control for accurate hand placement.

#pragma once

#include "ecs/components/GripPose.hpp"

#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

/// Number of arm-chain bones the FK hold pose drives, ordered root → tip.
inline constexpr std::size_t kArmHoldBoneCount = 4;

/// Mixamo bone-name suffixes for the arm chain (prefixed with
/// `mixamorig:<Side>` at runtime), ordered root → tip.
inline constexpr std::array<const char*, kArmHoldBoneCount> kArmHoldBoneSuffixes{
    "Shoulder", "Arm", "ForeArm", "Hand"};

/// Human-readable labels for the arm-chain bones (tweaker UI).
inline constexpr std::array<const char*, kArmHoldBoneCount> kArmHoldBoneDisplayNames{
    "Shoulder (clavicle)", "Upper Arm", "Forearm", "Hand (wrist)"};

/// @brief One arm's authored 3-DOF FK pose.
///
/// `boneAngles[i]` is the (pitch, yaw, roll) for arm-chain bone `i` (root → tip:
/// Shoulder, UpperArm, ForeArm, Hand). `fingerAngles` reuses the GripPose joint
/// layout (5 fingers × 4 joints, see `GripPose::index`).
///
/// All angles are DEGREES. `.x = pitch` (local Z), `.y = yaw` (local Y),
/// `.z = roll` (twist about local X).
struct ArmHoldPose
{
    std::array<glm::vec3, kArmHoldBoneCount> boneAngles{};
    std::array<glm::vec3, kGripPoseJointCount> fingerAngles{};
};

/// @brief Authored per-weapon third-person hold pose (see file header).
struct WeaponHoldPose
{
    glm::vec3 spineOffset{0.0f};                     ///< Spine2-local weapon translation (rig/model units).
    glm::quat spineRotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< Spine2-local weapon rotation.
    ArmHoldPose rightArm;
    ArmHoldPose leftArm;
};

/// @brief Load a hold pose from a TOML side-table (see WeaponHoldPose.cpp for the
/// schema). Missing sections keep their default (zeroed) values; returns false
/// only when the file cannot be parsed at all.
bool loadWeaponHoldPose(const std::string& path, WeaponHoldPose& out);

/// @brief Save a hold pose to a TOML file (atomic temp-write + rename).
/// Returns false on filesystem failure.
bool saveWeaponHoldPose(const std::string& path, const WeaponHoldPose& pose);
