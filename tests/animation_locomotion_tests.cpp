#include "client/animation/AnimationLocomotion.hpp"
#include "ecs/physics/TitanfallConstants.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>

namespace
{

bool near(float a, float b, float eps = 0.001f)
{
    return std::abs(a - b) <= eps;
}

void expect(bool condition)
{
    if (!condition)
        std::abort();
}

void testSpeedRefsTrackMovementConstants()
{
    assert(near(anim_locomotion::k_walkSpeedRef, tms::k_walkStrafeSpeed));
    assert(near(anim_locomotion::k_runSpeedRef, tms::k_walkForwardSpeed));
}

void testPureStrafeAtGameplayStrafeSpeedUsesWalkStrafe()
{
    const anim_locomotion::LocalVelocity local{.forward = 0.0f, .right = tms::k_walkStrafeSpeed};
    const anim_locomotion::LocomotionSelection sel = anim_locomotion::selectLocomotion(local, false);

    assert(sel.strafeClip == ClipId::StrafeLeftWalk);
    assert(sel.strafeBlend > 0.95f);
}

void testForwardGameplaySpeedUsesRun()
{
    const anim_locomotion::LocalVelocity local{.forward = tms::k_walkForwardSpeed, .right = 0.0f};
    const anim_locomotion::LocomotionSelection sel = anim_locomotion::selectLocomotion(local, false);

    assert(sel.primary == ClipId::Run);
    assert(sel.secondary == ClipId::Run);
    assert(near(sel.secondaryWeight, 0.0f));
}

void testCrouchForwardAndBackwardUseDifferentClips()
{
    const anim_locomotion::LocomotionSelection forward =
        anim_locomotion::selectLocomotion(anim_locomotion::LocalVelocity{.forward = 200.0f, .right = 0.0f}, true);
    const anim_locomotion::LocomotionSelection backward =
        anim_locomotion::selectLocomotion(anim_locomotion::LocalVelocity{.forward = -200.0f, .right = 0.0f}, true);

    assert(forward.secondary == ClipId::CrouchWalk);
    assert(backward.secondary == ClipId::CrouchWalkBackward);
    assert(forward.secondary != backward.secondary);
}

void testStandingBackwardUsesBackwardClips()
{
    // Walk-tier backpedal blends Idle -> WalkBackward (a real reverse walk, not a
    // forward clip spun around).
    const auto walkBack = anim_locomotion::selectLocomotion(
        anim_locomotion::LocalVelocity{.forward = -(anim_locomotion::k_walkSpeedRef * 0.5f), .right = 0.0f}, false);
    assert(walkBack.secondary == ClipId::WalkBackward);

    // Run-tier backpedal blends WalkBackward -> RunBackward, symmetric with the
    // forward Walk -> Run blend; never a forward-facing clip.
    const auto runBack = anim_locomotion::selectLocomotion(
        anim_locomotion::LocalVelocity{
            .forward = -0.5f * (anim_locomotion::k_walkSpeedRef + anim_locomotion::k_runSpeedRef), .right = 0.0f},
        false);
    assert(runBack.primary == ClipId::WalkBackward);
    assert(runBack.secondary == ClipId::RunBackward);
}

void testTransitionTrackerDetectsStartStopAndPivot()
{
    anim_locomotion::TransitionTracker tracker;

    anim_locomotion::TransitionIntent none =
        anim_locomotion::updateTransitionTracker(tracker, anim_locomotion::LocalVelocity{}, 1.0f / 128.0f);
    assert(none.kind == anim_locomotion::TransitionKind::None);

    anim_locomotion::TransitionIntent start = anim_locomotion::updateTransitionTracker(
        tracker, anim_locomotion::LocalVelocity{.forward = 0.0f, .right = tms::k_walkStrafeSpeed}, 1.0f / 128.0f);
    assert(start.kind == anim_locomotion::TransitionKind::Start);
    assert(start.preferredClip == ClipId::StartRight);

    (void)anim_locomotion::updateTransitionTracker(
        tracker, anim_locomotion::LocalVelocity{.forward = 0.0f, .right = tms::k_walkStrafeSpeed}, 1.0f / 128.0f);

    anim_locomotion::TransitionIntent pivot = anim_locomotion::updateTransitionTracker(
        tracker, anim_locomotion::LocalVelocity{.forward = 0.0f, .right = -tms::k_walkStrafeSpeed}, 1.0f / 128.0f);
    assert(pivot.kind == anim_locomotion::TransitionKind::Pivot);
    assert(pivot.preferredClip == ClipId::PivotLeft);

    anim_locomotion::TransitionIntent stop =
        anim_locomotion::updateTransitionTracker(tracker, anim_locomotion::LocalVelocity{}, 1.0f / 128.0f);
    assert(stop.kind == anim_locomotion::TransitionKind::Stop);
    assert(stop.preferredClip == ClipId::StopLeft);
}

void testDirectionalYawMapsApexAuthoredLeftAxisToGameplayDirections()
{
    const glm::vec3 apexAuthoredForward{-1.0f, 0.0f, 0.0f};
    const glm::vec3 gameForward{0.0f, 0.0f, 1.0f};
    const glm::vec3 gameRight{1.0f, 0.0f, 0.0f};
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    constexpr float pi = 3.14159265358979323846f;

    const auto yaw = [&](anim_locomotion::LocalVelocity local) {
        return anim_locomotion::directionalYawFromLocalVelocity(local, apexAuthoredForward, gameForward, gameRight, up);
    };

    expect(near(yaw({.forward = 300.0f, .right = 0.0f}), pi * 0.5f));
    expect(near(yaw({.forward = -300.0f, .right = 0.0f}), -pi * 0.5f));
    expect(near(std::abs(yaw({.forward = 0.0f, .right = 300.0f})), pi));
    expect(near(yaw({.forward = 0.0f, .right = -300.0f}), 0.0f));
    expect(near(yaw({.forward = 0.0f, .right = 0.0f}), 0.0f));
}

} // namespace

int main()
{
    testSpeedRefsTrackMovementConstants();
    testPureStrafeAtGameplayStrafeSpeedUsesWalkStrafe();
    testForwardGameplaySpeedUsesRun();
    testCrouchForwardAndBackwardUseDifferentClips();
    testStandingBackwardUsesBackwardClips();
    testTransitionTrackerDetectsStartStopAndPivot();
    testDirectionalYawMapsApexAuthoredLeftAxisToGameplayDirections();
    return 0;
}
