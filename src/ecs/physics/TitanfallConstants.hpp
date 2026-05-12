/// @file TitanfallConstants.hpp
/// @brief Titanfall-inspired movement constants adapted to Quake units.

#pragma once

/// @brief Titanfall-inspired movement constants, adapted to Quake units.
///
/// All speeds in units/second, distances in units, times in seconds.
/// Tuned to replicate the TMS (Titanfall Movement System) feel within our
/// custom swept-AABB engine at 128 Hz physics.
///
/// See docs/titanfall-movement-design.md for conversion rationale.
namespace tms
{

// Ground movement speeds

constexpr float k_walkSpeed =
    550.0f; ///< Max wish speed when walking (u/s). Sprint removed; this is the only base speed.
constexpr float k_sprintSpeed = 550.0f; ///< Deprecated: sprint removed. Kept equal to k_walkSpeed for safety.
constexpr float k_crouchSpeed = 350.0f; ///< Max wish speed when crouching (u/s).

// Jumping

constexpr float k_jumpSpeed = 330.0f;       ///< Upward velocity on ground jump (u/s). Must mirror physics::k_jumpSpeed.
constexpr float k_doubleJumpSpeed = 300.0f; ///< Upward velocity on air jump (u/s).
constexpr float k_slidehopJumpSpeed = 250.0f;    ///< Upward velocity when jumping during slide (u/s).
constexpr float k_doubleJumpCooldown = 0.10f;    ///< Min time after first jump before double jump is allowed (s).
constexpr float k_doubleJumpHorizBoost = 400.0f; ///< Horizontal velocity (u/s) toward WASD wishDir on double jump.
                                                 ///< Replaces horizontal velocity with wishDir * max(this, currentHs)
                                                 ///< so the second jump becomes an air dash. If no WASD held,
                                                 ///< horizontal momentum is preserved.
constexpr float k_doubleJumpGroundedRefreshTime = 0.5f; ///< Continuous time grounded (s) needed to refresh DJ.
                                                        ///< DJ does NOT refresh from wall jump, climb, ledge,
                                                        ///< slidehop, or instant landing — only by being on the
                                                        ///< ground (OnFoot) for this long. Counters DJ-dash spam.

// Coyote time

constexpr float k_coyoteTime = 0.15f; ///< Grace period after leaving ground/wall to still jump (s).

// Jump lurch

constexpr int k_enableJumpLurch = 1;               ///< Master enable for jump lurch (1 = enabled, 0 = disabled).
constexpr float k_jumpLurchMinGroundedTime = 0.3f; ///< Min continuous grounded time before a ground jump re-arms lurch
                                                   ///< (s). Prevents lurch from firing on bhop-chain jumps where the
                                                   ///< player only touches the ground for 1-2 ticks between hops.
constexpr float k_jumpLurchGraceMin = 0.2f;        ///< Time after jump where lurch is at max strength (s).
constexpr float k_jumpLurchGraceMax = 0.5f;        ///< Time after jump where lurch is disabled entirely (s).
constexpr float k_jumpLurchStrength = 5.0f;        ///< Multiplier for lurch intensity.
constexpr float k_jumpLurchMax = 400.0f;           ///< Maximum lurch velocity magnitude (u/s).
constexpr float k_jumpLurchBaseVelocity = 100.0f;  ///< Base lurch velocity before scaling (u/s).
constexpr float k_jumpLurchSpeedLoss = 0.125f;     ///< Fraction of speed lost on lurch (12.5%).

// Sliding

constexpr float k_slideMinStartSpeed = 300.0f;       ///< Min horizontal speed to enter slide (u/s).
constexpr float k_slideMinSpeed = 100.0f;            ///< Slide cancels below this speed (u/s).
constexpr float k_slideBoostMin = 180.0f;            ///< Min speed boost on slide entry (u/s).
constexpr float k_slideBoostMax = 380.0f;            ///< Max speed boost on slide entry (u/s).
constexpr float k_slideBoostCooldown = 1.5f;         ///< Cooldown between slide boosts (s).
constexpr float k_slideBrakingDecelMin = 200.0f;     ///< Initial braking deceleration (u/s^2).
constexpr float k_slideBrakingDecelMax = 400.0f;     ///< Maximum braking deceleration (u/s^2).
constexpr float k_slideBrakingRampTime = 3.0f;       ///< Time to ramp from min to max braking (s).
constexpr float k_slideFloorInfluenceForce = 400.0f; ///< How much slope angle affects slide speed (u/s^2).
constexpr float k_slideSteerAccel = 200.0f;          ///< Sideways accel from WASD while sliding (u/s^2).
                                                     ///< Applied along the component of wishDir perpendicular
                                                     ///< to current motion, so it gently rotates the slide
                                                     ///< trajectory without adding forward speed.
constexpr int k_slideFatigueDecayTicks = 200;        ///< Ticks (at 128Hz = 3s) to reset one fatigue level.
constexpr int k_slideFatigueMax = 4;                 ///< Max fatigue levels (boost fully killed at this).

// Wallrunning

constexpr float k_wallrunCheckDist = 35.0f;        ///< Sphere-cast distance for side walls (u).
constexpr float k_wallrunSphereRadius = 12.0f;     ///< Sphere-cast radius for wall detection (u).
constexpr float k_wallrunMinGroundDist = 50.0f;    ///< Min height above ground to wallrun (u).
constexpr float k_wallrunMaxSpeed = 800.0f;        ///< Max speed while wallrunning (u/s).
constexpr float k_wallrunAccel = 500.0f;           ///< Forward acceleration along wall (u/s^2).
constexpr float k_wallrunPushForce = 800.0f;       ///< Force pushing player toward wall (u/s^2).
constexpr float k_wallrunKickoffDuration = 1.75f;  ///< Max time on same wall before kickoff (s).
constexpr float k_wallrunSpeedLossDelay = 0.2f;    ///< Delay before clamping speed on wall (s).
constexpr float k_wallrunIntentThreshold = 0.1f;   ///< Min dot(wishDir, -wallNormal) to ENTER a wallrun.
                                                   ///< 0.1 ≈ 84° off-axis tolerance.
constexpr float k_wallrunDetachThreshold = -0.26f; ///< Min dot(wishDir, -wallNormal) to STAY on the wall.
                                                   ///< Negative = 15° grace past the wall plane, so the
                                                   ///< player can start looking away before detaching.
                                                   ///< cos(105°) ≈ -0.26.
constexpr float k_wallrunGripTime = 1.0f;          ///< Initial zero-gravity "grip" phase on a wall (s).
                                                   ///< During this window the player is pinned (vel.y = 0);
                                                   ///< after it, gravity leaks in gradually.
constexpr float k_wallrunGravityRampTime = 2.0f;   ///< Time to ramp gravity 0 → full after grip ends (s).
                                                   ///< Produces a natural slide-off so the player can't
                                                   ///< wallrun indefinitely even before the hard kickoff.
constexpr float k_wallJumpUpForce = 320.0f;        ///< Upward velocity on wall jump (u/s).
constexpr float k_wallJumpSideForce = 350.0f;      ///< Sideways velocity on wall jump (away from wall) (u/s).
constexpr float k_wallrunExitTime = 0.2f;          ///< Duration of "exiting wall" flag after leaving (s).
constexpr float k_wallrunCameraTilt = 7.5f;        ///< Camera roll when wallrunning (degrees).
constexpr float k_wallrunCameraTiltSpeed = 10.0f;  ///< Interpolation speed for camera tilt.

// Climbing

constexpr float k_climbCheckDist = 35.0f;          ///< Forward sphere-cast distance (u).
constexpr float k_climbSphereRadius = 12.0f;       ///< Sphere-cast radius for climb detection (u).
constexpr float k_climbMaxSpeed = 280.0f;          ///< Max upward climbing speed (u/s).
constexpr float k_climbMinSpeed = 180.0f;          ///< Min climbing speed (after decay) (u/s).
constexpr float k_climbKickoffDuration = 2.0f;     ///< Max climb time on same wall (s).
constexpr float k_climbMaxWallLookAngle = 30.0f;   ///< Max angle (degrees) between look dir and wall normal.
constexpr float k_climbSidewaysMultiplier = 0.1f;  ///< Sideways movement reduction while climbing.
constexpr float k_climbJumpUpForce = 320.0f;       ///< Upward velocity on climb jump (u/s).
constexpr float k_climbJumpBackForce = 350.0f;     ///< Backward velocity on climb jump (u/s).
constexpr float k_climbMinGroundDist = 40.0f;      ///< Min height above ground to start climbing (u).
constexpr float k_climbExitTime = 0.5f;            ///< Duration of "exiting climb" flag (s).
constexpr float k_climbRegrabLowerHeight = 400.0f; ///< Must be this much lower to regrab same wall (u).

// Ledge grabbing

constexpr float k_ledgeCheckDist = 35.0f;      ///< Forward trace distance for ledge detection (u).
constexpr float k_ledgeSphereRadius = 12.0f;   ///< Sphere-cast radius for ledge traces (u).
constexpr float k_ledgeMaxGrabDist = 35.0f;    ///< Max distance from ledge surface to grab (u).
constexpr float k_ledgeMinHoldTime = 0.5f;     ///< Min time frozen on ledge before release (s).
constexpr float k_ledgeMoveAccel = 800.0f;     ///< Acceleration pulling player toward ledge (u/s^2).
constexpr float k_ledgeMaxSpeed = 400.0f;      ///< Max speed of pull toward ledge (u/s).
constexpr float k_ledgeJumpUpForce = 350.0f;   ///< Upward velocity on ledge jump / mantle (u/s).
constexpr float k_ledgeJumpBackForce = 120.0f; ///< Backward velocity on ledge jump (u/s).
constexpr float k_ledgeExitTime = 0.5f;        ///< Duration of "exiting ledge" flag (s).

// Speed cap

constexpr float k_speedCap = 7000.0f; ///< Hard horizontal speed limit (u/s).

// Player dimensions

constexpr float k_standingHalfHeight = 36.0f;  ///< Standing AABB half-height (u).
constexpr float k_crouchingHalfHeight = 22.0f; ///< Crouching/sliding AABB half-height (u).

// Grappling hook (Widowmaker-style: direct pull → look-biased launch)

constexpr float k_grappleMaxRange = 4000.0f;  ///< Max hook distance (~20 m in Quake units).
constexpr float k_grapplePullSpeed = 4000.0f; ///< Direct velocity toward anchor (u/s). Overrides, not additive.
constexpr float k_grappleDetachDist = 80.0f;  ///< Auto-detach when this close to anchor (~2 m).
constexpr float k_grappleMaxDuration = 5.0f;  ///< Safety timeout (s).
constexpr float k_grappleCooldown = 5.0f;     ///< Cooldown between grapples (s).
constexpr float k_grappleLaunchLookBias =
    0.6f; ///< Look-direction weight on detach launch (0 = pure grapple dir, 1 = pure look).
constexpr float k_grappleLaunchSpeedMult = 1.15f; ///< Speed multiplier on launch (slight boost for momentum).

// Grapple perch (hold-jump → arc lands you above the hook point).
//
// The trajectory is stateless — computed each tick from current pos +
// grapplePoint only. Avoids the position jitter that a per-tick `t`
// accumulator (in PlayerSimState) would cause during reconciliation
// replay, since PlayerSimState is server-only and drifts on each apply.

constexpr float k_grapplePerchFeetOffset = 50.0f; ///< Feet height above the hook point in perch mode (u).
                                                  ///< Lets you grapple to a corner/wall and land on top of the platform
                                                  ///< rather than slamming into the surface you hooked.
constexpr float k_grapplePerchVerticalGain = 8.0f; ///< Vertical-pull P-controller gain (1/s). Final vy is
                                                   ///< clamped to ±k_grapplePullSpeed so very tall arcs are bounded.
constexpr float k_grapplePerchRiseRange = 100.0f;  ///< Altitude diff (u) below target at which horizontal speed
                                                   ///< bottoms out — controls how steep the rise phase feels.
constexpr float k_grapplePerchMinHorizFactor = 0.25f; ///< Floor on horizontal-speed throttle while below target.
                                                      ///< 0.0 = freezes XZ when below; 1.0 = no rise bias at all.

} // namespace tms
