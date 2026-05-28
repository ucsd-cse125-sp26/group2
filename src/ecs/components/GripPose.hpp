/// @file GripPose.hpp
/// @brief Authored per-weapon hand grip poses (Phase C of the AAA IK overhaul).

#pragma once

#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

/// Number of bones authored per finger. Mixamo finger chains are 4 joints
/// long (thumb1..thumb4, etc.) — the last joint is the tip and is the
/// rotation-bearing leaf even though it has no children of its own.
inline constexpr std::size_t kGripPoseBonesPerFinger = 4;

/// Number of fingers per hand: thumb, index, middle, ring, pinky.
inline constexpr std::size_t kGripPoseFingerCount = 5;

/// Total joint count per hand grip pose (5 fingers × 4 joints = 20 joints).
inline constexpr std::size_t kGripPoseJointCount = kGripPoseBonesPerFinger * kGripPoseFingerCount;

/// @brief Authored local-space finger angles describing how a hand wraps a weapon.
///
/// Order: thumb[0..3], index[0..3], middle[0..3], ring[0..3], pinky[0..3].
/// Each entry is a (pitch, yaw) pair in DEGREES, applied in the bone's
/// parent-local frame:
///   - `pitch` (the x component) = curl angle around the Mixamo Z axis,
///     positive = bend toward the palm (curl into a fist).
///   - `yaw` (the y component) = splay angle around the Mixamo Y axis,
///     positive = spread away from the neighbour finger.
/// There is no roll DOF — fingers cannot physically twist about their own
/// length. This 2-DOF parameterization keeps the authored data biomechanically
/// valid by construction: there's no slider that would let an editor produce
/// an impossible finger pose.
///
/// CharacterAnimator builds a local-space quaternion `qYaw * qPitch` from
/// these angles per joint, then slerps the animated finger pose toward this
/// target by `holdWeight` so idle-clip fingers smoothly transition to the
/// authored grip whenever a weapon is held.
struct GripPose
{
    /// (pitch, yaw) in degrees per joint. `.x = pitch (curl)`, `.y = yaw (splay)`.
    std::array<glm::vec2, kGripPoseJointCount> jointAngles{};

    /// @brief Helper: index into jointAngles for (finger, joint).
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
/// File format (pitch / yaw degrees per joint):
/// @code
/// [right_hand.thumb]
/// joints = [[pitch, yaw], [pitch, yaw], [pitch, yaw], [pitch, yaw]]
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

/// @brief Save a grip pose back to a TOML file.
///
/// Writes the same shape the loader expects. Sections for a hand are omitted
/// when the matching hand is invalid (so a single-hand TOML round-trips
/// correctly). Returns false on filesystem failure; the caller is expected to
/// log appropriately.
bool saveWeaponGripPoseToml(const std::string& path,
                            const WeaponGripPose& pose,
                            bool rightHandValid,
                            bool leftHandValid);
