/// @file GripPose.hpp
/// @brief Authored per-weapon hand grip poses (Phase C of the AAA IK overhaul).

#pragma once

#include <array>
#include <cstddef>
#include <glm/gtc/quaternion.hpp>
#include <string>

/// Number of bones authored per finger. Mixamo finger chains are 4 joints
/// long (thumb1..thumb4, etc.) — the last joint is the tip and is the
/// rotation-bearing leaf even though it has no children of its own.
inline constexpr std::size_t kGripPoseBonesPerFinger = 4;

/// Number of fingers per hand: thumb, index, middle, ring, pinky.
inline constexpr std::size_t kGripPoseFingerCount = 5;

/// Total joint count per hand grip pose (20 quats).
inline constexpr std::size_t kGripPoseJointCount = kGripPoseBonesPerFinger * kGripPoseFingerCount;

/// @brief Authored local-space finger rotations describing how a hand wraps a weapon.
///
/// Order: thumb[0..3], index[0..3], middle[0..3], ring[0..3], pinky[0..3].
/// Each quat is the bone's rotation in its parent bone's local frame —
/// the same space as ozz's per-joint local transforms. CharacterAnimator
/// blends from the animated local rotation toward this target by `holdWeight`,
/// producing a per-frame finger pose that smoothly transitions from
/// idle-animation fingers to weapon-gripping fingers as the player holds
/// or releases a weapon.
///
/// Replaces the per-frame iterative finger IK; this is the AAA pattern
/// for hand-on-weapon contact — author the pose once per weapon, blend in.
struct GripPose
{
    std::array<glm::quat, kGripPoseJointCount> jointRotations{};

    /// @brief Helper: index into jointRotations for (finger, joint).
    /// @param finger 0=thumb, 1=index, 2=middle, 3=ring, 4=pinky.
    /// @param joint  0..3 (root to tip).
    [[nodiscard]] static constexpr std::size_t index(std::size_t finger, std::size_t joint) noexcept
    {
        return finger * kGripPoseBonesPerFinger + joint;
    }
};

/// @brief Per-weapon pair of hand grip poses.
struct WeaponGripPose
{
    GripPose rightHand;
    GripPose leftHand;
    bool rightHandValid = false;
    bool leftHandValid = false;
};

/// @brief Load a weapon grip pose from a TOML side-table.
///
/// File format (Euler degrees XYZ, intrinsic rotation order):
/// @code
/// [right_hand.thumb]
/// joints = [[x, y, z], [x, y, z], [x, y, z], [x, y, z]]
/// [right_hand.index]
/// joints = [...]
/// # ... middle, ring, pinky
/// [left_hand.thumb]
/// joints = [...]
/// # ... (sections may be omitted; missing hand stays invalid + falls back
/// # to the animated finger pose at runtime)
/// @endcode
///
/// @return True if the file parsed successfully (at least one hand valid).
bool loadWeaponGripPose(const std::string& path, WeaponGripPose& out);
