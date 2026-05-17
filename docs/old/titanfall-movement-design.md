# Titanfall Movement System — Design Decisions

Adapted from the TMS (Titanfall Movement System for Unreal Engine) documentation
to our custom Quake-unit, swept-AABB, C++23 engine.

## Unit Conversion

TMS uses Unreal Engine units (1 UE = 1 cm). Our engine uses Quake units (1 QU ~ 1 inch = 2.54 cm).
Raw conversion: `QU = UE / 2.54`. However, values are tuned for feel rather than exact conversion.
Player capsule half-heights are nearly identical (TMS: 88 UE = 34.6 QU, Ours: 36 QU), so ratios transfer well.

## Features Implemented

### 1. Sprint
- Hold Left Shift to sprint. Only when grounded, moving forward, not crouching.
- Walk speed: 320 u/s (down from 400 to make sprint feel distinct)
- Sprint speed: 530 u/s (1.66x walk)
- Crouch speed: 200 u/s

### 2. Double Jump
- One air jump available after leaving the ground.
- Reset on: landing, starting wallrun, starting climb.
- Double jump speed: 340 u/s (slightly less than ground jump 380).

### 3. Coyote Time
- 0.15s grace period after leaving ground or wall where jump still works.
- Scales: faster movement = slightly longer grace (matches TMS 0.165s).
- Covers both ground coyote and wall coyote for wall jumps.

### 4. Jump Lurch
- After jumping, a brief window (0.2-0.5s) allows directional correction.
- If player presses a different WASD key than they had when jumping, velocity
  is redirected toward the new direction.
- Strength decays over the grace window. Speed is reduced by 12.5% as tradeoff.
- Lurch velocity: 60 u/s base, multiplied by strength (5.0), capped at 180 u/s.

### 5. Sliding
- Triggered by pressing crouch while sprinting above 400 u/s horizontal speed.
- Gives an initial speed boost (50-200 u/s, based on entry speed).
- Braking deceleration ramps from 200 to 400 u/s^2 over slide duration.
- Floor angle influences slide (downhill accelerates, uphill decelerates).
- Slide cancels below 100 u/s or on crouch release.
- Slidehop: jumping during slide gives reduced jump (280 u/s).
- Slide fatigue: consecutive slidehops reduce boost. Resets over time.
- Slide boost cooldown: 2.0s between boosts (prevents spam).
- Ground friction is zero during slide.

### 6. Wallrunning
- Automatic when airborne, near a wall, holding W, above min ground distance.
- Sphere-cast (radius 12) traces left/right from player to detect walls.
- Check distance: 35 units from player center.
- Min ground distance: 50 units (must be well off the ground).
- Movement: accelerate along wall direction up to 630 u/s max speed.
- Wallrun acceleration: 800 u/s^2 along the wall.
- Push toward wall: constant force keeps player stuck to wall surface.
- Gravity: zero during wallrun (faithful to Titanfall 2).
- Kickoff timer: 1.75s max on same wall, then forced off.
- Wall jump: up force 320 u/s + side force 350 u/s (away from wall).
- Wall blacklist: can't regrab the same wall unless at a lower height.
- Resets double jump on wallrun start.
- Speed loss delay: 0.1s before clamping to max speed (allows wallkick tech).

### 7. Climbing
- Triggered when airborne, wall in front, looking at wall (within 30 deg).
- Forward sphere-cast (radius 12) detects climbable walls.
- Climb speed: starts at 280 u/s, decays to 180 u/s over kickoff duration.
- Kickoff timer: 1.5s max climb on same wall.
- Sideways movement limited to 10% (prevents diagonal climbing).
- Climb jump: up 320 u/s + backward 350 u/s (away from wall).
- Climb blacklist: can't reclimb same wall unless at a significantly lower point.
- Resets double jump on climb start.

### 8. Ledge Grabbing
- When climbing reaches the top of a wall, detect the ledge surface.
- Player freezes on the ledge, gravity disabled.
- Min hold time: 0.5s before player can release.
- On movement input after min hold: mantle up (jump up 380 u/s).
- On jump: ledge jump (up 380 u/s + back 120 u/s).
- Can't grab the same ledge twice in a row.

### 9. Source-Like Air Strafing
- Keep existing Quake air acceleration (accel=5.0, wishspeed=300).
- This already enables strafe jumping / air strafing.
- TMS uses SVAccelerate=2 (weaker than Source's 10) for speed preservation.
- Our value of 5.0 is a good middle ground — keep as-is.

### 10. Speed Cap
- Hard cap at 1200 u/s horizontal speed.
- Only caps XZ velocity, Y (vertical) is uncapped.
- Applied after all movement calculations each tick.

## Architecture Decisions

### State Machine
PlayerState gains a `MoveMode` enum: `OnFoot, Sliding, WallRunning, Climbing, LedgeGrabbing`.
Movement system dispatches to mode-specific handlers each tick.

### Sphere Cast
Full swept-sphere vs world geometry (not degenerate point cast).
- Sphere vs Plane: expand plane by radius, ray test.
- Sphere vs AABB: inflate AABB by radius, ray-slab test.
- Sphere vs Brush: expand each brush plane by radius, interval test.

### Wall Detection
New module `WallDetection.hpp/cpp` with `detectWalls()` function.
Called each tick when airborne. Casts spheres left/right/forward.
Returns which walls are present + their normals + hit points.

### Movement System Signature Change
`runMovement(registry, dt)` becomes `runMovement(registry, dt, worldGeometry)`
because wall detection needs to raycast against world geometry.

### File Organization
- `TitanfallConstants.hpp` — all tuning constants in one place
- `PlayerState.hpp` — extended with MoveMode + all state fields
- `WallDetection.hpp/cpp` — sphere-cast wall queries
- `MovementSystem.cpp` — rewritten as state-machine dispatcher
- `SweptCollision.hpp/cpp` — gains `sphereCast()` function

## Adapted Constants Table

| Constant | TMS Value (UE) | Our Value (QU) | Notes |
|----------|---------------|----------------|-------|
| WalkSpeed | 600 | 320 | Reduced from 400 to differentiate sprint |
| SprintSpeed | 1150 | 530 | ~1.66x walk |
| CrouchSpeed | 400 | 200 | |
| JumpSpeed | 575 | 380 | Keep existing (good feel) |
| DoubleJumpSpeed | - | 340 | Slightly less than ground |
| SlidehopJumpSpeed | 430 | 280 | Reduced during slides |
| Gravity | 980*1.17 | 1000 | Keep existing |
| WallrunMaxSpeed | 1600 | 630 | |
| WallrunAccel | 20m/s | 800 u/s^2 | |
| ClimbMaxSpeed | 550 | 280 | |
| ClimbMinSpeed | 350 | 180 | |
| SpeedCap | 2500 | 1200 | |
| WallJumpUp | 460 | 320 | |
| WallJumpSide | 800 | 350 | |
| CoyoteTime | 0.165s | 0.15s | |
| WallrunKickoff | 1.75s | 1.75s | |
| ClimbKickoff | 1.5s | 1.5s | |
| SlideMinStartSpeed | 850 | 400 | ~sprint speed |
| SlideBrakingMin | 375 | 200 u/s^2 | |
| SlideBrakingMax | 750 | 400 u/s^2 | |

## Test Map Additions

New geometry for testing all mechanics:
- **Wallrun corridors**: Parallel walls (200u tall, 400u long) with gaps for wall-to-wall jumps
- **Climb walls**: Tall flat walls (300u tall) for vertical climbing
- **Ledge walls**: Medium walls (120u) with flat tops for ledge grabbing
- **Slide runs**: Long flat stretches + downhill ramps for slide momentum
- **Combined course**: Mixed geometry for chaining mechanics
