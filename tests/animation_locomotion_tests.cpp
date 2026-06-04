#include "client/animation/AnimationLibrary.hpp"
#include "client/animation/AnimationLocomotion.hpp"
#include "ecs/physics/TitanfallConstants.hpp"

#include <cassert>
#include <cmath>

namespace
{

bool near(float a, float b, float eps = 0.001f)
{
    return std::abs(a - b) <= eps;
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

void testR301UpperBodyClipIdsAreCatalogued()
{
    static_assert(static_cast<int>(ClipId::R301IdleUpper) < static_cast<int>(ClipId::_Count));
    static_assert(static_cast<int>(ClipId::R301ReloadUpper) < static_cast<int>(ClipId::_Count));
    static_assert(ClipId::R301IdleUpper != ClipId::R301ReloadUpper);
}

} // namespace

int main()
{
    testSpeedRefsTrackMovementConstants();
    testPureStrafeAtGameplayStrafeSpeedUsesWalkStrafe();
    testForwardGameplaySpeedUsesRun();
    testCrouchForwardAndBackwardUseDifferentClips();
    testTransitionTrackerDetectsStartStopAndPivot();
    testR301UpperBodyClipIdsAreCatalogued();
    return 0;
}
