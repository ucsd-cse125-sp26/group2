/// @file AnimationLocomotion.cpp
/// @brief Pure locomotion clip/transition planner for CharacterAnimator.

#include "AnimationLocomotion.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace anim_locomotion
{

namespace
{

constexpr float k_startDurationSec = 0.16f;
constexpr float k_stopDurationSec = 0.18f;
constexpr float k_pivotDurationSec = 0.20f;
constexpr float k_startPeakWeight = 0.30f;
constexpr float k_stopPeakWeight = 0.35f;
constexpr float k_pivotPeakWeight = 0.42f;

[[nodiscard]] float dot2(const LocalVelocity& a, const LocalVelocity& b) noexcept
{
    return a.forward * b.forward + a.right * b.right;
}

[[nodiscard]] LocalVelocity normalizedOrZero(LocalVelocity v) noexcept
{
    const float len = speed(v);
    if (len <= 0.001f)
        return {};
    v.forward /= len;
    v.right /= len;
    return v;
}

[[nodiscard]] glm::vec3 normalizedPlanarOr(glm::vec3 v, glm::vec3 up, const glm::vec3& fallback) noexcept
{
    const float upLen = glm::length(up);
    up = upLen > 0.0001f ? up / upLen : glm::vec3{0.0f, 1.0f, 0.0f};
    v -= up * glm::dot(v, up);
    const float len = glm::length(v);
    if (len > 0.0001f)
        return v / len;
    return fallback;
}

[[nodiscard]] float signedPlanarAngle(const glm::vec3& from, const glm::vec3& to, const glm::vec3& up) noexcept
{
    return std::atan2(glm::dot(glm::cross(from, to), up), glm::dot(from, to));
}

[[nodiscard]] ClipId dominantStartClip(const LocalVelocity& local) noexcept
{
    if (std::abs(local.right) > std::abs(local.forward))
        return local.right >= 0.0f ? ClipId::StartRight : ClipId::StartLeft;
    if (local.forward < 0.0f)
        return ClipId::StartBackward;
    return ClipId::StartForward;
}

[[nodiscard]] ClipId dominantStopClip(const LocalVelocity& local) noexcept
{
    if (std::abs(local.right) > std::abs(local.forward))
        return local.right >= 0.0f ? ClipId::StopRight : ClipId::StopLeft;
    if (local.forward < 0.0f)
        return ClipId::StopBackward;
    return ClipId::StopForward;
}

[[nodiscard]] ClipId pivotClipForTurn(const LocalVelocity& previous, const LocalVelocity& current) noexcept
{
    const float cross = previous.right * current.forward - previous.forward * current.right;
    if (std::abs(current.right) >= std::abs(current.forward))
        return current.right >= 0.0f ? ClipId::PivotRight : ClipId::PivotLeft;
    return cross >= 0.0f ? ClipId::PivotRight : ClipId::PivotLeft;
}

} // namespace

float speed(const LocalVelocity& local) noexcept
{
    return std::sqrt(local.forward * local.forward + local.right * local.right);
}

LocalVelocity localVelocityFromWorld(const glm::vec3& velocityWorld, float yawRad) noexcept
{
    const float cosYaw = std::cos(yawRad);
    const float sinYaw = std::sin(yawRad);
    const glm::vec3 forward{sinYaw, 0.0f, cosYaw};
    const glm::vec3 right{cosYaw, 0.0f, -sinYaw};
    return {
        .forward = velocityWorld.x * forward.x + velocityWorld.z * forward.z,
        .right = velocityWorld.x * right.x + velocityWorld.z * right.z,
    };
}

float directionalYawFromLocalVelocity(const LocalVelocity& local,
                                      const glm::vec3& authoredForward,
                                      const glm::vec3& gameForward,
                                      const glm::vec3& gameRight,
                                      const glm::vec3& up,
                                      float idleCutoff) noexcept
{
    if (speed(local) <= idleCutoff)
        return 0.0f;

    const float upLen = glm::length(up);
    const glm::vec3 upAxis = upLen > 0.0001f ? up / upLen : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::vec3 source = normalizedPlanarOr(authoredForward, upAxis, glm::vec3{0.0f, 0.0f, 1.0f});
    const glm::vec3 fwd = normalizedPlanarOr(gameForward, upAxis, source);
    glm::vec3 fallbackRight = glm::cross(upAxis, fwd);
    if (glm::length(fallbackRight) <= 0.0001f)
        fallbackRight = glm::cross(upAxis, glm::vec3{0.0f, 0.0f, 1.0f});
    fallbackRight = normalizedPlanarOr(fallbackRight, upAxis, glm::vec3{1.0f, 0.0f, 0.0f});
    const glm::vec3 right = normalizedPlanarOr(gameRight, upAxis, fallbackRight);

    glm::vec3 desired = fwd * local.forward + right * local.right;
    desired = normalizedPlanarOr(desired, upAxis, source);
    return signedPlanarAngle(source, desired, upAxis);
}

float smoothingAlpha(float dtSec, float tauSec) noexcept
{
    if (dtSec <= 0.0f || tauSec <= 0.0f)
        return 0.0f;
    return 1.0f - std::exp(-dtSec / tauSec);
}

LocomotionSelection selectLocomotion(const LocalVelocity& local, bool crouching) noexcept
{
    LocomotionSelection out;
    const float horizontalSpeed = speed(local);
    out.horizontalSpeed = horizontalSpeed;

    const bool reverseLike = local.forward < -k_idleCutoff;
    if (crouching) {
        if (horizontalSpeed < k_idleCutoff) {
            out.primary = ClipId::CrouchIdle;
            out.secondary = ClipId::CrouchIdle;
            out.secondaryWeight = 0.0f;
        } else {
            out.primary = ClipId::CrouchIdle;
            out.secondary = reverseLike ? ClipId::CrouchWalkBackward : ClipId::CrouchWalk;
            out.secondaryWeight =
                std::clamp((horizontalSpeed - k_idleCutoff) / (k_walkSpeedRef - k_idleCutoff), 0.0f, 1.0f);
        }

        if (std::abs(local.right) >= k_idleCutoff)
            out.strafeClip = local.right > 0.0f ? ClipId::CrouchWalkLeft : ClipId::CrouchWalkRight;
    } else {
        if (horizontalSpeed < k_idleCutoff) {
            out.primary = ClipId::Idle;
            out.secondary = ClipId::Idle;
            out.secondaryWeight = 0.0f;
        } else if (horizontalSpeed < k_walkSpeedRef) {
            out.primary = ClipId::Idle;
            out.secondary = reverseLike ? ClipId::RunBackward : ClipId::Walk;
            out.secondaryWeight =
                std::clamp((horizontalSpeed - k_idleCutoff) / (k_walkSpeedRef - k_idleCutoff), 0.0f, 1.0f);
        } else if (horizontalSpeed < k_runSpeedRef) {
            out.primary = reverseLike ? ClipId::RunBackward : ClipId::Walk;
            out.secondary = reverseLike ? ClipId::RunBackward : ClipId::Run;
            out.secondaryWeight =
                reverseLike
                    ? 0.0f
                    : std::clamp((horizontalSpeed - k_walkSpeedRef) / (k_runSpeedRef - k_walkSpeedRef), 0.0f, 1.0f);
        } else {
            out.primary = reverseLike ? ClipId::RunBackward : ClipId::Run;
            out.secondary = out.primary;
            out.secondaryWeight = 0.0f;
        }

        if (std::abs(local.right) >= k_idleCutoff) {
            const bool right = local.right > 0.0f;
            const bool walkStrafe = horizontalSpeed <= k_walkSpeedRef + 0.5f;
            if (walkStrafe)
                out.strafeClip = right ? ClipId::StrafeLeftWalk : ClipId::StrafeRightWalk;
            else
                out.strafeClip = right ? ClipId::StrafeLeft : ClipId::StrafeRight;
        }
    }

    out.strafeBlend = (horizontalSpeed > k_idleCutoff && out.strafeClip != ClipId::_Count)
                          ? std::clamp(std::abs(local.right) / std::max(horizontalSpeed, 1.0f), 0.0f, 1.0f)
                          : 0.0f;

    if (horizontalSpeed < k_idleCutoff) {
        out.speedScale = 1.0f;
    } else {
        const float refSpeed =
            horizontalSpeed < k_walkSpeedRef
                ? k_walkSpeedRef
                : (horizontalSpeed < k_runSpeedRef
                       ? k_walkSpeedRef + ((horizontalSpeed - k_walkSpeedRef) / (k_runSpeedRef - k_walkSpeedRef)) *
                                              (k_runSpeedRef - k_walkSpeedRef)
                       : k_runSpeedRef);
        out.speedScale = horizontalSpeed / std::max(refSpeed, 1.0f);
    }

    return out;
}

TransitionIntent updateTransitionTracker(TransitionTracker& tracker, const LocalVelocity& local, float dtSec) noexcept
{
    (void)dtSec;

    const float currentSpeed = speed(local);
    const bool moving = currentSpeed >= k_moveThreshold;
    const LocalVelocity prev = tracker.previous;
    const bool wasMoving = tracker.wasMoving;

    TransitionIntent intent;
    if (!wasMoving && moving) {
        intent.kind = TransitionKind::Start;
        intent.preferredClip = dominantStartClip(local);
        intent.durationSec = k_startDurationSec;
        intent.peakWeight = k_startPeakWeight;
    } else if (wasMoving && !moving) {
        intent.kind = TransitionKind::Stop;
        intent.preferredClip = dominantStopClip(prev);
        intent.durationSec = k_stopDurationSec;
        intent.peakWeight = k_stopPeakWeight;
    } else if (wasMoving && moving) {
        const LocalVelocity prevDir = normalizedOrZero(prev);
        const LocalVelocity curDir = normalizedOrZero(local);
        const float turnDot = dot2(prevDir, curDir);
        const bool hardDirectionChange = turnDot < -0.35f;
        const bool hardStrafeSwap = std::abs(prev.right) >= k_moveThreshold &&
                                    std::abs(local.right) >= k_moveThreshold &&
                                    ((prev.right < 0.0f) != (local.right < 0.0f));
        if (hardDirectionChange || hardStrafeSwap) {
            intent.kind = TransitionKind::Pivot;
            intent.preferredClip = pivotClipForTurn(prev, local);
            intent.durationSec = k_pivotDurationSec;
            intent.peakWeight = k_pivotPeakWeight;
        }
    }

    tracker.wasMoving = moving;
    if (moving)
        tracker.previous = local;

    return intent;
}

float transitionWeight(TransitionKind kind, float elapsedSec, float durationSec, float peakWeight) noexcept
{
    if (kind == TransitionKind::None || durationSec <= 0.0f || peakWeight <= 0.0f)
        return 0.0f;
    const float t = std::clamp(elapsedSec / durationSec, 0.0f, 1.0f);
    return std::sin(t * 3.14159265358979323846f) * peakWeight;
}

float transitionPlaybackRatio(float elapsedSec, float durationSec) noexcept
{
    if (durationSec <= 0.0f)
        return 0.0f;
    return std::clamp(elapsedSec / durationSec, 0.0f, 1.0f);
}

ClipId fallbackTransitionClip(TransitionKind kind, ClipId preferredClip) noexcept
{
    switch (kind) {
    case TransitionKind::Start:
    case TransitionKind::Stop:
        return ClipId::SlowRun;
    case TransitionKind::Pivot:
        if (preferredClip == ClipId::PivotLeft)
            return ClipId::TurnLeft90;
        if (preferredClip == ClipId::PivotRight)
            return ClipId::TurnRight90;
        return ClipId::TurnRight90;
    case TransitionKind::None:
        break;
    }
    return ClipId::_Count;
}

} // namespace anim_locomotion
