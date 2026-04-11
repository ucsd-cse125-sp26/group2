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

// ═══════════════════════════════════════════════════════════════════════════
// Ground movement speeds
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_walkSpeed = 320.0f;   ///< Max wish speed when walking (u/s).
constexpr float k_sprintSpeed = 530.0f; ///< Max wish speed when sprinting (u/s).
constexpr float k_crouchSpeed = 200.0f; ///< Max wish speed when crouching (u/s).

// ═══════════════════════════════════════════════════════════════════════════
// Jumping
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_jumpSpeed = 380.0f;         ///< Upward velocity on ground jump (u/s).
constexpr float k_doubleJumpSpeed = 340.0f;   ///< Upward velocity on air jump (u/s).
constexpr float k_slidehopJumpSpeed = 280.0f; ///< Upward velocity when jumping during slide (u/s).

// ═══════════════════════════════════════════════════════════════════════════
// Coyote time
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_coyoteTime = 0.15f; ///< Grace period after leaving ground/wall to still jump (s).

// ═══════════════════════════════════════════════════════════════════════════
// Jump lurch
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_jumpLurchGraceMin = 0.2f;      ///< Time after jump where lurch is at max strength (s).
constexpr float k_jumpLurchGraceMax = 0.5f;      ///< Time after jump where lurch is disabled entirely (s).
constexpr float k_jumpLurchStrength = 5.0f;      ///< Multiplier for lurch intensity.
constexpr float k_jumpLurchMax = 180.0f;         ///< Maximum lurch velocity magnitude (u/s).
constexpr float k_jumpLurchBaseVelocity = 60.0f; ///< Base lurch velocity before scaling (u/s).
constexpr float k_jumpLurchSpeedLoss = 0.125f;   ///< Fraction of speed lost on lurch (12.5%).

// ═══════════════════════════════════════════════════════════════════════════
// Sliding
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_slideMinStartSpeed = 400.0f;       ///< Min horizontal speed to enter slide (u/s).
constexpr float k_slideMinSpeed = 100.0f;            ///< Slide cancels below this speed (u/s).
constexpr float k_slideBoostMin = 50.0f;             ///< Min speed boost on slide entry (u/s).
constexpr float k_slideBoostMax = 200.0f;            ///< Max speed boost on slide entry (u/s).
constexpr float k_slideBoostCooldown = 2.0f;         ///< Cooldown between slide boosts (s).
constexpr float k_slideBrakingDecelMin = 200.0f;     ///< Initial braking deceleration (u/s^2).
constexpr float k_slideBrakingDecelMax = 400.0f;     ///< Maximum braking deceleration (u/s^2).
constexpr float k_slideBrakingRampTime = 3.0f;       ///< Time to ramp from min to max braking (s).
constexpr float k_slideFloorInfluenceForce = 400.0f; ///< How much slope angle affects slide speed (u/s^2).
constexpr int k_slideFatigueDecayTicks = 384;        ///< Ticks (at 128Hz = 3s) to reset one fatigue level.
constexpr int k_slideFatigueMax = 4;                 ///< Max fatigue levels (boost fully killed at this).

// ═══════════════════════════════════════════════════════════════════════════
// Wallrunning
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_wallrunCheckDist = 35.0f;       ///< Sphere-cast distance for side walls (u).
constexpr float k_wallrunSphereRadius = 12.0f;    ///< Sphere-cast radius for wall detection (u).
constexpr float k_wallrunMinGroundDist = 50.0f;   ///< Min height above ground to wallrun (u).
constexpr float k_wallrunMaxSpeed = 630.0f;       ///< Max speed while wallrunning (u/s).
constexpr float k_wallrunAccel = 800.0f;          ///< Forward acceleration along wall (u/s^2).
constexpr float k_wallrunPushForce = 300.0f;      ///< Force pushing player toward wall (u/s^2).
constexpr float k_wallrunKickoffDuration = 1.75f; ///< Max time on same wall before kickoff (s).
constexpr float k_wallrunSpeedLossDelay = 0.1f;   ///< Delay before clamping speed on wall (s).
constexpr float k_wallJumpUpForce = 320.0f;       ///< Upward velocity on wall jump (u/s).
constexpr float k_wallJumpSideForce = 350.0f;     ///< Sideways velocity on wall jump (away from wall) (u/s).
constexpr float k_wallrunExitTime = 0.2f;         ///< Duration of "exiting wall" flag after leaving (s).
constexpr float k_wallrunCameraTilt = 7.5f;       ///< Camera roll when wallrunning (degrees).
constexpr float k_wallrunCameraTiltSpeed = 10.0f; ///< Interpolation speed for camera tilt.

// ═══════════════════════════════════════════════════════════════════════════
// Climbing
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_climbCheckDist = 35.0f;          ///< Forward sphere-cast distance (u).
constexpr float k_climbSphereRadius = 12.0f;       ///< Sphere-cast radius for climb detection (u).
constexpr float k_climbMaxSpeed = 280.0f;          ///< Max upward climbing speed (u/s).
constexpr float k_climbMinSpeed = 180.0f;          ///< Min climbing speed (after decay) (u/s).
constexpr float k_climbKickoffDuration = 1.5f;     ///< Max climb time on same wall (s).
constexpr float k_climbMaxWallLookAngle = 30.0f;   ///< Max angle (degrees) between look dir and wall normal.
constexpr float k_climbSidewaysMultiplier = 0.1f;  ///< Sideways movement reduction while climbing.
constexpr float k_climbJumpUpForce = 320.0f;       ///< Upward velocity on climb jump (u/s).
constexpr float k_climbJumpBackForce = 350.0f;     ///< Backward velocity on climb jump (u/s).
constexpr float k_climbMinGroundDist = 40.0f;      ///< Min height above ground to start climbing (u).
constexpr float k_climbExitTime = 0.5f;            ///< Duration of "exiting climb" flag (s).
constexpr float k_climbRegrabLowerHeight = 400.0f; ///< Must be this much lower to regrab same wall (u).

// ═══════════════════════════════════════════════════════════════════════════
// Ledge grabbing
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_ledgeCheckDist = 35.0f;      ///< Forward trace distance for ledge detection (u).
constexpr float k_ledgeSphereRadius = 12.0f;   ///< Sphere-cast radius for ledge traces (u).
constexpr float k_ledgeMaxGrabDist = 35.0f;    ///< Max distance from ledge surface to grab (u).
constexpr float k_ledgeMinHoldTime = 0.5f;     ///< Min time frozen on ledge before release (s).
constexpr float k_ledgeMoveAccel = 800.0f;     ///< Acceleration pulling player toward ledge (u/s^2).
constexpr float k_ledgeMaxSpeed = 400.0f;      ///< Max speed of pull toward ledge (u/s).
constexpr float k_ledgeJumpUpForce = 380.0f;   ///< Upward velocity on ledge jump / mantle (u/s).
constexpr float k_ledgeJumpBackForce = 120.0f; ///< Backward velocity on ledge jump (u/s).
constexpr float k_ledgeExitTime = 0.5f;        ///< Duration of "exiting ledge" flag (s).

// ═══════════════════════════════════════════════════════════════════════════
// Speed cap
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_speedCap = 1200.0f; ///< Hard horizontal speed limit (u/s).

// ═══════════════════════════════════════════════════════════════════════════
// Player dimensions
// ═══════════════════════════════════════════════════════════════════════════

constexpr float k_standingHalfHeight = 36.0f;  ///< Standing AABB half-height (u).
constexpr float k_crouchingHalfHeight = 22.0f; ///< Crouching/sliding AABB half-height (u).

} // namespace tms
