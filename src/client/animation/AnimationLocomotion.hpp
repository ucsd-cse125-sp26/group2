/// @file AnimationLocomotion.hpp
/// @brief Pure locomotion clip/transition planner for CharacterAnimator.

#pragma once

#include "AnimationLibrary.hpp"
#include "ecs/physics/TitanfallConstants.hpp"

#include <glm/vec3.hpp>

namespace anim_locomotion
{

inline constexpr float k_idleCutoff = 10.0f;
inline constexpr float k_moveThreshold = 45.0f;
inline constexpr float k_walkSpeedRef = tms::k_walkStrafeSpeed;
inline constexpr float k_runSpeedRef = tms::k_walkForwardSpeed;

struct LocalVelocity
{
    float forward = 0.0f;
    float right = 0.0f;
};

struct LocomotionSelection
{
    ClipId primary = ClipId::Idle;
    ClipId secondary = ClipId::Idle;
    ClipId strafeClip = ClipId::_Count;
    float secondaryWeight = 0.0f;
    float strafeBlend = 0.0f;
    float speedScale = 1.0f;
    float horizontalSpeed = 0.0f;
};

enum class TransitionKind : unsigned char
{
    None,
    Start,
    Stop,
    Pivot,
};

struct TransitionIntent
{
    TransitionKind kind = TransitionKind::None;
    ClipId preferredClip = ClipId::_Count;
    float durationSec = 0.0f;
    float peakWeight = 0.0f;
};

struct TransitionTracker
{
    bool wasMoving = false;
    LocalVelocity previous{};
};

[[nodiscard]] float speed(const LocalVelocity& local) noexcept;
[[nodiscard]] LocalVelocity localVelocityFromWorld(const glm::vec3& velocityWorld, float yawRad) noexcept;
[[nodiscard]] float directionalYawFromLocalVelocity(const LocalVelocity& local,
                                                    const glm::vec3& authoredForward,
                                                    const glm::vec3& gameForward,
                                                    const glm::vec3& gameRight,
                                                    const glm::vec3& up,
                                                    float idleCutoff = k_idleCutoff) noexcept;
[[nodiscard]] float smoothingAlpha(float dtSec, float tauSec) noexcept;
[[nodiscard]] LocomotionSelection selectLocomotion(const LocalVelocity& local, bool crouching) noexcept;
[[nodiscard]] TransitionIntent
updateTransitionTracker(TransitionTracker& tracker, const LocalVelocity& local, float dtSec) noexcept;
[[nodiscard]] float
transitionWeight(TransitionKind kind, float elapsedSec, float durationSec, float peakWeight) noexcept;
[[nodiscard]] float transitionPlaybackRatio(float elapsedSec, float durationSec) noexcept;
[[nodiscard]] ClipId fallbackTransitionClip(TransitionKind kind, ClipId preferredClip) noexcept;

} // namespace anim_locomotion
