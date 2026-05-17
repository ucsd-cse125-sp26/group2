# Physics & Movement System Documentation

Comprehensive reference for the physics engine, collision detection, and
Titanfall-inspired movement state machine. This is the core game mechanic --
everything the player feels comes from the code documented here.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Coordinate System](#2-coordinate-system)
3. [Quake-Style Movement Physics](#3-quake-style-movement-physics)
4. [Swept AABB Collision](#4-swept-aabb-collision)
5. [CollisionSystem (runCollision)](#5-collisionsystem-runcollision)
6. [Movement State Machine (Titanfall-Inspired)](#6-movement-state-machine-titanfall-inspired)
7. [World Geometry](#7-world-geometry)
8. [Physics Constants](#8-physics-constants)
9. [Client-Server Parity](#9-client-server-parity)

---

## 1. Architecture Overview

The physics system is organized into three distinct layers. Keeping them
separate makes each layer independently testable and guarantees that the
client and server run byte-identical physics.

```
Layer 1 - Pure Math (no ECS, no registry)
    src/ecs/physics/
        Movement.hpp / .cpp       Gravity, friction, PM_Accelerate, clipVelocity, wishDir
        SweptCollision.hpp / .cpp  Swept AABB, swept sphere, HitResult
        WallDetection.hpp / .cpp   Sphere-cast wall/climb/ledge queries
        PhysicsConstants.hpp       Quake-engine tuning values
        TitanfallConstants.hpp     Titanfall movement tuning values
        WorldData.hpp              Static world geometry (planes, boxes, brushes)

Layer 2 - ECS Systems (read/write components via Registry)
    src/ecs/systems/
        MovementSystem.hpp / .cpp  State machine: velocity updates, mode transitions
        CollisionSystem.hpp / .cpp Position integration via swept AABB, depenetration

Layer 3 - Game Loop Integration
    Fixed-timestep loop calls:
        1. runMovement(registry, dt, world)   -- update velocity
        2. runCollision(registry, dt, world)  -- integrate position, resolve contacts
```

### Data Flow Per Tick

```
InputSnapshot ──> MovementSystem ──> Velocity (updated)
                                         |
                                         v
                       CollisionSystem ──> Position (integrated)
                                         |
                                         v
                                    PlayerState.grounded (set)
```

**Key rule:** MovementSystem never writes Position. CollisionSystem owns
position integration. This prevents velocity and position from disagreeing
when a collision clip changes the direction of motion.

### Components Involved

| Component | File | Role |
|---|---|---|
| `Position` | `Position.hpp` | `glm::vec3` world-space centre of the AABB |
| `Velocity` | `Velocity.hpp` | `glm::vec3` current velocity (u/s) |
| `CollisionShape` | `CollisionShape.hpp` | `glm::vec3 halfExtents` -- AABB half-dimensions |
| `PlayerState` | `PlayerState.hpp` | `MoveMode`, `grounded`, timers, blacklists, etc. |
| `InputSnapshot` | `InputSnapshot.hpp` | WASD, jump, crouch, sprint, grapple, yaw/pitch |

---

## 2. Coordinate System

| Axis | Direction | Convention |
|---|---|---|
| **X** | Left/Right | Positive X = rightward (camera-relative varies with yaw) |
| **Y** | Up/Down | **Positive Y = up** |
| **Z** | Forward/Back | **Positive Z = forward** (at yaw=0) |

**Units:** Quake units (qu). 1 qu ~ 1 inch ~ 2.54 cm. Player is 72 qu tall
standing (half-height 36), 32 qu wide (half-extent 16).

**Yaw:** Radians. At yaw=0 the player faces +Z. Yaw increases
counter-clockwise when viewed from above (standard trigonometric convention).

**Pitch:** Radians. Clamped to [-89 deg, +89 deg] (~1.5533 rad) by the input
sampling system. Negative pitch = looking up, positive pitch = looking down.

---

## 3. Quake-Style Movement Physics

All functions live in the `physics` namespace (`Movement.hpp` / `.cpp`).
They are pure: take values in, return values out, no mutation via pointer.

### 3.1 Gravity

```cpp
glm::vec3 applyGravity(glm::vec3 vel, float dt);
```

Subtracts `k_gravity * dt` from `vel.y`. Called every tick when the entity is
airborne (not grounded).

- `k_gravity = 1000 u/s^2` -- faster than real-world 9.8 m/s^2 for snappy
  arc feel.
- Jump height formula: `apex = k_jumpSpeed^2 / (2 * k_gravity)`
  = 380^2 / 2000 = **72.2 units** (~6 feet).

### 3.2 Ground Friction

```cpp
glm::vec3 applyGroundFriction(glm::vec3 vel, float dt);
```

Applies friction to horizontal (XZ) velocity only. Y is unchanged.

**The `k_stopSpeed` trick:**

Standard friction `speed -= speed * friction * dt` never reaches zero -- it
asymptotically approaches it, causing the player to glide forever at low
speeds. The Quake trick replaces `speed` with `max(speed, k_stopSpeed)` in
the friction calculation:

```cpp
float control = max(speed, k_stopSpeed);   // amplify friction at low speeds
float drop    = control * k_friction * dt;
float newSpeed = max(0, speed - drop) / speed;
vel.xz *= newSpeed;
```

When `speed < k_stopSpeed` (150 u/s), the friction acts as if the player were
moving at 150 u/s, producing a much larger deceleration that brings them to a
**crisp, decisive stop** instead of an ice-rink glide.

### 3.3 PM_Accelerate

```cpp
glm::vec3 accelerate(glm::vec3 vel, glm::vec3 wishDir, float wishSpeed, float accel, float dt);
```

The heart of Quake/Source movement. This is the single function responsible
for strafe-jumping and air-strafing.

**Parameters:**

| Parameter | Ground value | Air value |
|---|---|---|
| `wishDir` | Normalised XZ direction from WASD + yaw | Same |
| `wishSpeed` | `k_maxGroundSpeed` (400) / walk (320) / sprint (530) | `k_airMaxSpeed` (300) |
| `accel` | `k_groundAccel` (15) | `k_airAccel` (5) |

**Algorithm:**

```cpp
float currentSpeed = dot(vel, wishDir);   // projection onto wish direction
float addSpeed     = wishSpeed - currentSpeed;
if (addSpeed <= 0) return vel;            // already at/above wish speed in this direction

float accelSpeed = min(accel * dt * wishSpeed, addSpeed);
return vel + wishDir * accelSpeed;
```

**Why this enables strafe-jumping:**

The critical insight is that `currentSpeed` is the *projection* of velocity
onto `wishDir`, not the total speed. When the player strafes diagonally (W+A
or W+D) while turning, `wishDir` is nearly perpendicular to their current
velocity. This makes `currentSpeed` close to zero, so `addSpeed` is large,
and the full `accelSpeed` is applied -- even though total speed is already
well above `wishSpeed`. The result is that each strafe-jump *adds* speed in
the new direction without capping the existing momentum.

On the ground, `k_groundAccel = 15` makes reaching max speed nearly instant
(within 2-3 frames). In the air, `k_airAccel = 5` is high enough for
meaningful air control but low enough to preserve momentum.

### 3.4 clipVelocity

```cpp
glm::vec3 clipVelocity(glm::vec3 vel, glm::vec3 normal, float overbounce);
```

Projects velocity onto a collision surface so the entity slides along it
rather than stopping dead or tunneling through.

```cpp
float backoff = dot(vel, normal) * overbounce;
return vel - normal * backoff;
```

- `overbounce = 1.0` (floors): pure projection, no bounce.
- `overbounce = 1.001` (walls/ceilings): slight separation impulse prevents
  getting stuck in corners where two planes meet.

### 3.5 computeWishDir

```cpp
glm::vec3 computeWishDir(float yaw, bool forward, bool back, bool left, bool right);
```

Converts WASD key state + yaw angle into a normalised world-space direction.

1. Build a local (moveX, moveZ) vector from key state:
   - W: moveZ += 1
   - S: moveZ -= 1
   - A: moveX += 1 (negated internally for camera convention)
   - D: moveX -= 1
2. If no keys pressed, return `(0, 0, 0)`.
3. Rotate by yaw:
   ```
   wish.x =  moveX * cos(yaw) + moveZ * sin(yaw)
   wish.z = -moveX * sin(yaw) + moveZ * cos(yaw)
   ```
4. Normalize and return. Y component is always 0.

---

## 4. Swept AABB Collision

All functions live in `physics` namespace (`SweptCollision.hpp` / `.cpp`).

### 4.1 Plane Convention

Throughout the codebase:

- `dot(normal, point) > distance` is **free space**.
- `dot(normal, point) < distance` is **solid**.
- The normal always points **into free space** (out of the solid).

Examples (Y-up):

| Surface | Normal | Distance |
|---|---|---|
| Floor at y=0 | (0, 1, 0) | 0 |
| Ceiling at y=512 | (0, -1, 0) | -512 |
| Wall at x=256 (solid right) | (-1, 0, 0) | -256 |

### 4.2 sweepAABB vs Infinite Planes

```cpp
HitResult sweepAABB(vec3 halfExtents, vec3 start, vec3 end, span<const Plane> planes);
```

Uses the **Minkowski sum** approach: instead of sweeping a box against a
plane, expand the plane outward by the AABB's support distance in the
plane's normal direction, then sweep a point (the AABB centre).

**Per plane:**

1. Compute support distance:
   `r = |normal.x| * halfExtents.x + |normal.y| * halfExtents.y + |normal.z| * halfExtents.z`

2. Compute signed distances from the AABB centre to the (unexpanded) plane:
   ```
   distStart = dot(normal, start) - distance
   distEnd   = dot(normal, end)   - distance
   ```

3. Skip if starting inside solid (`distStart < r`) -- depenetration handles
   this case separately.

4. Skip if moving away from the plane (`distEnd >= distStart`).

5. Compute intersection time:
   `t = (distStart - r) / (distStart - distEnd)`

6. Track the earliest hit across all planes.

### 4.3 sweepAABBvsBox

```cpp
HitResult sweepAABBvsBox(vec3 halfExtents, vec3 start, vec3 end, const WorldAABB& box);
```

Reduces a moving AABB vs static AABB to a ray vs expanded box test.

1. **Expand** the static box by the moving AABB's half-extents (Minkowski
   sum):
   ```
   expMin = box.min - halfExtents
   expMax = box.max + halfExtents
   ```

2. Skip if the centre point starts inside the expanded box (depenetration).

3. **Slab intersection** on each axis (X, Y, Z):
   - Compute entry time `t1` and exit time `t2` for each axis slab.
   - Track the latest entry (`tEntry`) and earliest exit (`tExit`).
   - Track which face normal corresponds to the latest entry.
   - If `tEntry > tExit` or `tExit < 0`, the sweep misses.

4. If `tEntry` is in `[0, 1)`, record a hit.

### 4.4 sweepAABBvsBrush

```cpp
HitResult sweepAABBvsBrush(vec3 halfExtents, vec3 start, vec3 end, const WorldBrush& brush);
```

A convex brush is the intersection of up to 8 half-spaces. The sweep enters
the brush when it simultaneously crosses all planes from outside to inside.

**Per brush plane:**

1. Expand by AABB support distance `r` (same Minkowski approach).
2. Compute adjusted distances:
   ```
   adjStart = dot(normal, start) - distance - r
   adjEnd   = dot(normal, end)   - distance - r
   ```
   Positive = outside (free space), negative = inside (solid).

3. If both endpoints are outside this plane, the sweep misses the brush
   entirely.

4. If both are inside, this plane doesn't constrain the interval.

5. If crossing: compute `t = adjStart / (adjStart - adjEnd)`.
   - Entering solid: update `tEntry` (latest).
   - Exiting solid: update `tExit` (earliest).

6. Must start outside the brush. If `tEntry < tExit` and `tEntry` is in
   `[0, 1)`, the sweep hits.

### 4.5 sweepAll

```cpp
HitResult sweepAll(vec3 halfExtents, vec3 start, vec3 end, const WorldGeometry& world);
```

Sweeps against all world geometry (planes, boxes, brushes) and returns the
earliest hit. Simple min-reduction over all individual sweep results.

### 4.6 Sphere Cast

```cpp
SphereHitResult sphereCast(float radius, vec3 start, vec3 end, const WorldGeometry& world);
```

Used for wall detection, climb detection, and ledge detection -- **not** for
player collision (which uses swept AABBs).

Same Minkowski-sum principle: geometry is expanded by the sphere radius, then
the sweep becomes a point test against the expanded geometry.

- **vs Planes:** Expand plane distance by `radius`. Ray-test centre point.
- **vs AABBs:** Inflate box by `radius` on all axes. Ray-slab test. This is
  slightly conservative at corners (inflated box instead of rounded box),
  which is acceptable and even desirable for generous wall detection.
- **vs Brushes:** Expand each brush plane by `radius`. Same half-space
  interval test.

Returns `SphereHitResult` which includes the world-space contact `point` on
the surface (useful for ledge positions).

---

## 5. CollisionSystem (runCollision)

```cpp
void runCollision(Registry& registry, float dt, const WorldGeometry& world);
```

Runs for every entity with `[Position, Velocity, CollisionShape, PlayerState]`.
This is where position is actually integrated. MovementSystem only sets velocity.

### Phase 0: Depenetration

Before any sweeping, push the entity out of any geometry it currently
overlaps. This handles edge cases where the entity was teleported inside a
brush, or where floating-point drift pushed it through a surface.

Three sub-passes run in order:

1. **depenetratePlanes:** For each infinite plane, if `dot(normal, pos) - dist < r`
   (the AABB overlaps the plane), push along the normal by
   `overlap + k_pushback` (0.03125 -- Quake's `DIST_EPSILON`). Cancel any
   velocity component going into the plane.

2. **depenetrateBox:** For each WorldAABB, check if the centre is inside the
   Minkowski-expanded box. If so, find the axis of **least penetration**
   (shortest escape distance among all 6 faces) and push out along that
   face's normal.

3. **depenetrateBrush:** For each WorldBrush, check if the centre is inside
   all half-spaces simultaneously. If so, find the plane with the smallest
   overlap and push out along its normal.

### Phase 1: Bump Loop (4 clips + stair stepping)

Quake-style iterative collision response. Up to 4 iterations:

```
for clip = 0..3:
    target = pos + vel * remainingTime
    hit    = sweepAll(halfExtents, pos, target, world)

    if no hit:
        pos = target
        break

    pos += vel * hit.tFirst * remainingTime
    remainingTime *= (1 - hit.tFirst)

    if hit is floor (normal.y > 0.7):
        push out by k_pushback
        clipVelocity with k_overbounceFloor (1.0)
        set grounded = true

    else (wall/ceiling):
        if was grounded and tryStepUp succeeds:
            accept step, break
        push out by k_pushback
        clipVelocity with k_overbounceWall (1.001)
```

**Stair stepping** (`tryStepUp`): When a wall is hit and the entity was
grounded on the previous tick:

1. Lift straight up by `k_stepHeight` (18u). Abort if ceiling blocks.
2. Sweep horizontally at the lifted height. Abort if still blocked.
3. Drop back down by `k_stepHeight`. Must land on a floor (normal.y > 0.7).
4. Accept the new position; zero vertical velocity.

This lets the player walk over small obstacles (steps, curbs) without
jumping.

### Phase 2: Slope Sticking

If the entity was grounded last tick and has horizontal velocity, probe
downward by `k_stepHeight` (18u). If a floor surface is found, snap position
down to it. This prevents the player from bouncing off downhill slopes and
descending stairs.

### Phase 3: Ground Probe

After all movement, sweep downward by `k_stepHeight` to detect floor
surfaces just below the entity. If a floor is found (normal.y > 0.7), set
`grounded = true` and record `groundNormal`. This catches cases where the
bump loop didn't hit a floor directly but the entity is resting on one.

---

## 6. Movement State Machine (Titanfall-Inspired)

Implemented in `MovementSystem.cpp`. Runs before CollisionSystem each tick.

### 6.1 MoveMode Enum

```cpp
enum class MoveMode : uint8_t {
    OnFoot,        // Normal ground/air movement (walk, sprint, crouch, airborne)
    Sliding,       // Momentum slide on ground
    WallRunning,   // Running along a wall surface
    Climbing,      // Climbing vertically up a wall
    LedgeGrabbing, // Holding onto a ledge at the top of a wall
};
```

Only one mode is active at a time. The system checks transitions in priority
order each tick: LedgeGrab > Climb > Wallrun > Slide.

### 6.2 Tick Execution Order

The `runMovement` function processes each player entity in this order:

| Step | Action | Notes |
|---|---|---|
| 0 | `tickTimers` | Count down all cooldowns and timers |
| 1 | `detectWalls` | Sphere-cast wall/climb/ledge queries (airborne or on wall/climb only) |
| 2 | `updateSprint` | Sprint on/off based on input + state |
| 3 | State transitions | Try enter: LedgeGrab, Climb, Wallrun, Slide (priority order) |
| 4 | Crouch transition | Enter crouch immediately; exit deferred to step 10 |
| 5 | Mode-specific movement | Dispatch to OnFoot / Sliding / WallRunning / Climbing / LedgeGrabbing |
| 5b | Grappling hook | Fire, pull, auto-release |
| 6 | `handleJump` | Ground, double, wall, climb, ledge, slidehop, coyote |
| 7 | `handleJumpLurch` | Directional correction window (air only) |
| 8 | `updateCoyoteTime` | Start coyote timer on ground-to-air transition |
| 9 | Landing reset | Clear jump count, blacklists, re-enable slide |
| 10 | Auto-uncrouch | Expand AABB if safe; collision-check before committing |
| 11 | `applySpeedCap` | Hard horizontal speed limit |
| 12 | Track jump key | Edge detection for next tick |

### 6.3 Sprint

Sprint requires: shift held, W held, grounded, not crouching, mode is OnFoot.
Sprint ends immediately when any condition is broken.

Wish speeds by state:

| State | Wish Speed (u/s) |
|---|---|
| Walking | 320 |
| Sprinting | 530 |
| Crouching | 200 |
| Sliding | 0 (no acceleration; momentum-based) |

### 6.4 Jumping

Multiple jump types, checked in priority order:

**Ledge Jump / Mantle (MoveMode::LedgeGrabbing):**
- Only after `k_ledgeMinHoldTime` (0.5s) on the ledge.
- Applies `k_ledgeJumpUpForce` (380 u/s) upward.
- Pushes along ledge normal by `k_ledgeJumpBackForce` (120 u/s) -- this
  actually pushes *over* the ledge since the normal faces away from the wall.
- Transitions to OnFoot, sets exitingLedge flag.

**Wall Jump (MoveMode::WallRunning):**
- Applies `k_wallJumpUpForce` (320 u/s) upward.
- Pushes along wall normal by `k_wallJumpSideForce` (350 u/s) -- away from wall.
- Blacklists the wall to prevent immediate regrab.
- Resets double jump.

**Climb Jump (MoveMode::Climbing):**
- Applies `k_climbJumpUpForce` (320 u/s) upward.
- Pushes along wall normal by `k_climbJumpBackForce` (350 u/s) -- away from wall.
- Blacklists the wall.
- Resets double jump.

**Coyote Wall Jump:**
- After leaving a wall within `k_coyoteTime` (0.15s), a jump still counts
  as a wall jump using the blacklisted wall's normal.
- Resets double jump.

**Slidehop (MoveMode::Sliding):**
- Applies `k_slidehopJumpSpeed` (280 u/s) -- lower than normal to keep
  slidehop chains grounded.
- Exits slide mode, defers uncrouch to step 10.
- Increments slide fatigue counter (diminishes future boosts).

**Ground Jump (grounded or coyote):**
- Applies `k_jumpSpeed` (380 u/s) upward.
- Sets `jumpCooldown = k_doubleJumpCooldown` (0.1s) to prevent instant double.
- Enables jump lurch window.
- Records WASD input at jump time for lurch direction comparison.

**Double Jump:**
- Requires: jump key re-pressed (rising edge, not held), cooldown expired,
  `jumpCount < 2`, `canDoubleJump` is true.
- If falling, resets `vel.y` to 0 before adding `k_doubleJumpSpeed` (340 u/s).
  This feels better than being additive when the player is falling fast.
- Sets `jumpCount = 2`, disables further double jumps.
- Re-enables jump lurch.

### 6.5 Coyote Time

A 0.15s grace period after leaving ground (or wall) where a jump input still
works. Only triggers on *involuntary* loss of ground (walking off an edge) --
not when the player actively jumps.

```
if wasGroundedLastTick && !grounded && moveMode == OnFoot && !jumpedThisTick:
    coyoteTimer = k_coyoteTime
```

### 6.6 Jump Lurch

Titanfall's signature mid-air directional correction mechanic.

**Setup:** On any jump (ground, double, or slidehop), record the WASD input
at jump time and start the lurch timer.

**Trigger:** While airborne, if the player presses a *different* WASD key
than they held at jump time, apply a velocity impulse in the new wish
direction.

**Strength decay:** Full strength for the first `k_jumpLurchGraceMin` (0.2s)
after jumping, linearly decays to zero at `k_jumpLurchGraceMax` (0.5s).

**Application:**
```
lurchMag = k_jumpLurchBaseVelocity(60) * k_jumpLurchStrength(5) * decay
lurchMag = min(lurchMag, k_jumpLurchMax(180))
vel.xz += wishDir * lurchMag
vel.xz *= (1 - k_jumpLurchSpeedLoss(0.125))  // 12.5% speed tax
```

Lurch is one-shot per jump. After it fires, it is disabled until the next
jump. This makes it a deliberate trade: direction change in exchange for
speed loss.

### 6.7 Sliding

**Entry conditions:** Mode is OnFoot, grounded, crouch pressed,
`canEnterSlide` is true, horizontal speed >= `k_slideMinStartSpeed` (400 u/s).

**On entry:**
- Switch to crouching AABB (half-height 22u, lowered 14u).
- Apply speed boost in the current horizontal direction:
  - Boost amount interpolated between `k_slideBoostMin` (80) and
    `k_slideBoostMax` (200) based on entry speed.
  - Scaled down by slide fatigue: `fatigueScale = 1 - fatigueCounter / fatigueMax`.
  - Only if `slideBoostCooldown` has expired (2s between boosts).

**Each tick while sliding:**
- No ground friction (momentum preservation).
- No wish-speed-driven acceleration (wish speed = 0).
- Braking deceleration ramps from `k_slideBrakingDecelMin` (200 u/s^2) to
  `k_slideBrakingDecelMax` (400 u/s^2) over `k_slideBrakingRampTime` (3s).
- **Slope influence:** On non-flat floors, gravity is projected onto the
  slope surface. Downhill adds speed, uphill subtracts speed. Force is
  `k_slideFloorInfluenceForce` (400 u/s^2).

**Exit conditions:** Crouch released, speed drops below `k_slideMinSpeed`
(100 u/s), or airborne.

**Slide Fatigue:** A counter (`slideFatigueCounter`) tracks consecutive
slidehops. Each slidehop increments it (max 4). At max fatigue, slide boost
is fully suppressed. The counter decays by one level every
`k_slideFatigueDecayTicks` (384 ticks = 3s at 128Hz) while not sliding.

**Slide Boost Cooldown:** After each boost application, a 2s cooldown
prevents boost spam even without fatigue.

**Camera:** Slight tilt based on lateral velocity relative to the look
direction (max 5 degrees).

### 6.8 Wallrunning

**Detection:** Each tick when airborne (or already wallrunning), sphere-casts
are fired left and right from the player's centre. Distance:
`k_wallrunCheckDist` (35u), sphere radius: `k_wallrunSphereRadius` (12u).
Only normals with `|normal.y| < 0.3` are accepted as walls.

**Entry conditions:** Mode is OnFoot, airborne, not exiting a wall,
W key held, ground distance > `k_wallrunMinGroundDist` (50u), wall detected,
wall not blacklisted.

**On entry:**
- Clamp vertical velocity to [-25, +25] u/s for a smooth grab feel.
- Compute `wallForward` direction as `cross(up, wallNormal)`, flipped to
  match the player's current direction of travel.
- Reset double jump.

**Each tick while wallrunning:**
- Accelerate along `wallForward` at `k_wallrunAccel` (800 u/s^2) up to
  `k_wallrunMaxSpeed` (630 u/s).
- **Speed loss delay:** For the first `k_wallrunSpeedLossDelay` (0.1s),
  speed is NOT clamped. If the player is already faster than 630 u/s (e.g.
  from a slidehop), they can wall-kick off almost immediately to preserve
  speed. This is the "wallkick tech" that rewards high-skill play.
- Push toward wall: `vel -= wallNormal * k_wallrunPushForce(300) * dt`.
- Zero gravity: `vel.y = 0` (faithful to Titanfall 2).
- Camera tilt: +7.5 degrees for right wall, -7.5 for left wall.

**Kickoff timer:** After `k_wallrunKickoffDuration` (1.75s) on the same wall,
the player is forced off.

**Exit conditions:** Kickoff timer expired, wall no longer detected, or
W released. On exit, sets `exitingWall = true` for `k_wallrunExitTime` (0.2s)
and starts a coyote timer for wall jumps.

**Wall blacklist:** On exit, the wall's normal and the player's Y position are
recorded. To regrab the same wall (dot product > 0.9), the player must be at
a **lower height** than when they last ran on it. This prevents infinite
wall-climbing by repeatedly wall-jumping and regrabbing the same wall higher.
The blacklist clears on landing.

### 6.9 Climbing

**Detection:** Each tick when airborne, a sphere-cast is fired forward from
the player's centre. Distance: `k_climbCheckDist` (35u), sphere radius:
`k_climbSphereRadius` (12u).

**Entry conditions:** Mode is OnFoot, airborne, not exiting a climb,
W key held, front wall detected, ground distance > `k_climbMinGroundDist`
(40u), player looking at the wall within `k_climbMaxWallLookAngle` (30 deg),
wall not blacklisted.

**On entry:**
- Reduce horizontal velocity to 10% (`k_climbSidewaysMultiplier`).
- Ensure `vel.y >= 0` (don't start climbing while falling hard).
- Reset double jump.

**Each tick while climbing:**
- Upward speed decays linearly from `k_climbMaxSpeed` (280 u/s) to
  `k_climbMinSpeed` (180 u/s) over `k_climbKickoffDuration` (1.5s).
- Sideways velocity multiplied by 0.1 each tick (prevents diagonal climbing).
- Push toward wall to stay attached.
- Camera tilt returns to zero.

**Exit conditions:** Kickoff timer expired (1.5s), front wall lost, or
W released. On exit, sets `exitingClimb = true` for `k_climbExitTime` (0.5s)
and starts a coyote timer.

**Climb blacklist:** Same mechanism as wallrun blacklist. The player must be
`k_climbRegrabLowerHeight` (400u) lower than their last grab point to
reclimb the same wall. Clears on landing.

### 6.10 Ledge Grabbing

**Detection:** While climbing, if a front wall is detected but a forward
sphere-cast from above the player's head (head + 10u) finds *no* wall, a
ledge exists. A downward probe from that elevated forward position locates
the ledge surface (must have `normal.y > 0.7` -- walkable).

**Entry conditions:** Currently climbing, ledge detected, not exiting a
ledge grab.

**On entry:**
- Record `ledgePoint` and `ledgeNormal`.
- Reset hold timer to 0.
- Reset double jump.

**Each tick while on ledge:**
- Velocity is zeroed (player is frozen).
- Gravity is effectively disabled (vel = 0).
- `ledgeHoldTimer` increments.
- **Auto-mantle:** After `k_ledgeMinHoldTime` (0.5s), if any movement key
  is pressed, the player is launched upward (`k_ledgeJumpUpForce` = 380 u/s)
  and pushed along the wall normal (`k_ledgeJumpBackForce` = 120 u/s). This
  effectively mantles over the ledge.
- Jump input also triggers a ledge jump (same forces) after the hold timer.

### 6.11 Grappling Hook

**Firing (tryFireGrapple):**
- Triggered on rising edge of the grapple key (E / middle mouse).
- Sphere-cast (radius 4u) from eye position in the look direction, max range
  `k_grappleMaxRange` (1500u).
- If a surface is hit, hook attaches at the hit point.

**Pull physics (handleGrapple):**
- Pull direction is a blend of direct-to-hook and look direction:
  `pullDir = normalize(directDir * 0.65 + lookDir * 0.35)`
  This creates a **pull + swing** effect: looking away from the hook causes
  the player to orbit around it rather than fly straight to it.
- Accelerate toward pull direction at `k_grapplePullAccel` (1800 u/s^2) up to
  `k_grapplePullSpeed` (900 u/s).
- Gravity is reduced to `k_grappleGravityScale` (0.15x) -- nearly floating.
- Speed cap is raised to `k_grappleSpeedCap` (1500 u/s) during grapple.
- Grapple cancels any active wallrun/climb/slide.

**Auto-release conditions:**
- Distance to hook < `k_grappleReleaseMinDist` (60u) -- arrived.
- Distance to hook > `k_grappleReleaseMaxDist` (2000u) -- overshot.
- Pull duration > `k_grappleMaxDuration` (7s) -- safety timeout.
- Crouch pressed -- manual cancel.
- Grapple key released -- manual cancel (hold to grapple).

**Cooldown:** `k_grappleCooldown` (1s) after release before the next grapple.

### 6.12 Speed Cap

Applied as the final step each tick. Hard clamps horizontal (XZ) speed:

- Normal: `k_speedCap` = 1200 u/s
- During grapple: `k_grappleSpeedCap` = 1500 u/s

Vertical velocity is never capped.

### 6.13 State Transition Diagram

```
                    +-----------+
                    |  OnFoot   |<----------- landing / exit any mode
                    +-----+-----+
                          |
         +----------------+------------------+
         |                |                  |
    grounded +       airborne +          airborne +
    crouch +         wall left/right     wall front +
    speed >= 400     W held              W held +
         |           not exiting wall    looking at wall
         v                |                  |
    +----------+    +-----v------+     +-----v-----+
    | Sliding  |    | WallRunning|     |  Climbing  |
    +-----+----+    +-----+------+     +------+-----+
          |               |                   |
     slidehop         wall jump          ledge detected
     (jump)           (jump)                  |
          |               |            +------v------+
          v               v            | LedgeGrabbing|
       OnFoot          OnFoot          +------+------+
                                              |
                                         mantle / jump
                                              |
                                              v
                                           OnFoot
```

Grapple is orthogonal -- it can activate from any mode and forces
transition to OnFoot.

---

## 7. World Geometry

### 7.1 Geometry Types

All defined in `SweptCollision.hpp`:

**Plane** -- An infinite plane dividing free space from solid:
```cpp
struct Plane {
    glm::vec3 normal;  // unit vector, points into free space
    float distance;    // dot(normal, point_on_plane) = distance
};
```

**WorldAABB** -- A static axis-aligned box:
```cpp
struct WorldAABB {
    glm::vec3 min;     // lowest x, y, z corner
    glm::vec3 max;     // highest x, y, z corner
};
```

**WorldBrush** -- A convex volume defined by up to 8 bounding planes (for
ramps, angled walls, etc.):
```cpp
struct WorldBrush {
    static constexpr int k_maxPlanes = 8;
    Plane planes[k_maxPlanes];
    int planeCount{0};
};
```
The solid interior is the intersection of all half-spaces: `dot(normal, p) < distance`.

**WorldGeometry** -- Container for all world collision data:
```cpp
struct WorldGeometry {
    std::span<const Plane> planes;
    std::span<const WorldAABB> boxes;
    std::span<const WorldBrush> brushes;
};
```

### 7.2 Helper Factories

**makeRamp(xMin, xMax, zMin, zMax, height):**

Creates a wedge brush that rises from ground level at `zMin` to `height` at
`zMax`. The slope normal is computed from the geometry:
```
depth    = zMax - zMin
len      = sqrt(depth^2 + height^2)
slopeNorm = (0, depth/len, -height/len)
```
Uses 6 planes: slope surface, bottom, left, right, front, back.

**makeDiagonalWall(center, halfLen, halfThick, height, dir):**

Creates an arbitrarily-oriented wall brush. The face normal is the 90-degree
rotation of `dir` in the XZ plane: `faceN = (dir.z, 0, -dir.x)`. Uses 6
planes: front face, back face, both ends, top, bottom.

### 7.3 testWorld() Layout

The test world is defined as static data in `WorldData.hpp` and returned by
`testWorld()`. Both client and server call this function, guaranteeing
identical geometry for prediction parity.

**Infinite planes (1):**
- Floor at y=0

**Axis-aligned boxes (30):**

| # | Description | Bounds (min -> max) | Purpose |
|---|---|---|---|
| 0 | Reference cube | (-32,0,368) to (32,64,432) | Visual reference, 64^3 |
| 1 | Small steppable box | (68,0,768) to (132,16,832) | 16u tall, auto-stepped |
| 2 | Large jumpable box | (-140,0,760) to (-60,64,840) | 64u tall, requires jump |
| 3-7 | Stairs (5 steps) | z=1500..1740, 16u/step, 128u wide | 5 steps, each 16u high x 48u deep |
| 8 | Axis-aligned wall | (-128,0,1892) to (128,120,1908) | 256w x 120h x 16d |
| 9 | Pole | (-258,0,1892) to (-242,200,1908) | 16x16 base, 200u tall |
| 10 | Elevated walkway | (-16,80,2100) to (16,96,2500) | 32w x 16h x 400 long, at y=80 |
| 11-12 | Wallrun corridor | x=+-100..116, z=2700..3100 | Parallel walls, 200u tall, 200u apart |
| 13-14 | Wall-to-wall jump | Offset walls z=3300..3700 | For chaining wallruns |
| 15 | Climb wall | (-64,0,3900) to (64,300,3916) | 300u tall, for climb testing |
| 16 | Ledge wall | (200,0,3900) to (328,120,3916) | 120u tall, ledge grab target |
| 17-18 | Slide run guides | x=+-184..200, z=4100..4600 | Side walls for slide testing |
| 19 | Course platform | (-48,0,4800) to (48,48,4848) | Jump-up entry to parkour course |
| 20-21 | Course wallrun walls | z=4900..5400 | Angled course wallrun |
| 22 | Course climb target | (-64,0,5500) to (64,250,5516) | End-of-course climb wall |
| 23 | Ledge platform | (200,0,5500) to (328,100,5516) | Course ledge target |
| 24 | Landing pad | (-80,0,5550) to (80,16,5650) | Beyond climb wall |
| 25-26 | Arch pillars | x=+-84..116, z=6000..6032 | 32x32 base, 500u tall |
| 27 | Arch crossbar | (-116,460,6000) to (116,500,6032) | Connects pillars at y=460 |
| 28 | High platform | (-100,400,6200) to (100,416,6300) | Grapple target, y=400 |
| 29 | Ultra-high platform | (-48,580,6500) to (48,596,6548) | Grapple target, y=580 |

**Convex brushes (3):**

| # | Description | Geometry | Purpose |
|---|---|---|---|
| 0 | Gentle ramp (15 deg) | x=[-214,-86], z=[950,1250], rises to 80u | Walkable slope |
| 1 | Steep ramp (40 deg) | x=[86,214], z=[1000,1200], rises to 168u | Steep slope test |
| 2 | Diagonal wall (45 deg) | Centre (300,0,1900), length 200, thick 16, height 120 | Angled wall collision |

**Map layout along +Z axis (forward from spawn at origin):**

```
z ~    0 : Spawn point (origin)
z ~  400 : Reference cube (64^3)
z ~  800 : Small steppable box + large jumpable box
z ~ 1000 : Gentle ramp (left) + steep ramp (right)
z ~ 1500 : Stairs (5 steps)
z ~ 1900 : Axis-aligned wall, diagonal wall, pole
z ~ 2100 : Elevated walkway
z ~ 2700 : Wallrun corridor (parallel walls)
z ~ 3300 : Wall-to-wall jump section
z ~ 3900 : Climb wall + ledge wall
z ~ 4100 : Slide run (long flat stretch)
z ~ 4800 : Combined parkour course
z ~ 6000 : Grapple test arch + high platforms
```

---

## 8. Physics Constants

### 8.1 Core Physics (`PhysicsConstants.hpp`)

All values in Quake units. Y-up coordinate system.

| Constant | Value | Unit | Notes |
|---|---|---|---|
| `k_gravity` | 1000.0 | u/s^2 | Faster than real for snappy arcs |
| `k_jumpSpeed` | 380.0 | u/s | Apex ~72u (~6 ft). **Must be tuned with k_gravity:** `apex = v^2 / (2g)` |
| `k_maxGroundSpeed` | 400.0 | u/s | Max horizontal ground speed |
| `k_groundAccel` | 15.0 | dimensionless | Ground acceleration multiplier. Higher = faster ramp-up |
| `k_airAccel` | 5.0 | dimensionless | Air acceleration. Higher than Quake (0.7), lower than HL2 (10) |
| `k_airMaxSpeed` | 300.0 | u/s | Wish-speed cap in air. Does NOT cap total speed |
| `k_friction` | 4.0 | dimensionless | Ground friction coefficient (Quake default) |
| `k_stopSpeed` | 150.0 | u/s | Friction amplified below this for crisp stopping |
| `k_overbounceWall` | 1.001 | dimensionless | Wall/ceiling separation impulse |
| `k_overbounceFloor` | 1.0 | dimensionless | Floor overbounce (no bounce) |
| `k_stepHeight` | 18.0 | u | Max auto-step height |

### 8.2 Titanfall Movement (`TitanfallConstants.hpp`)

All in namespace `tms`. Speeds in u/s, distances in u, times in seconds.

**Ground Speeds:**

| Constant | Value | Notes |
|---|---|---|
| `k_walkSpeed` | 320 u/s | Reduced from 400 to differentiate sprint |
| `k_sprintSpeed` | 530 u/s | 1.66x walk |
| `k_crouchSpeed` | 200 u/s | Slow crouch-walk |

**Jumping:**

| Constant | Value | Notes |
|---|---|---|
| `k_jumpSpeed` | 380 u/s | Ground jump upward velocity |
| `k_doubleJumpSpeed` | 340 u/s | Slightly less than ground jump |
| `k_slidehopJumpSpeed` | 280 u/s | Reduced during slide |
| `k_doubleJumpCooldown` | 0.10 s | Prevents instant double-jump |

**Coyote Time:**

| Constant | Value |
|---|---|
| `k_coyoteTime` | 0.15 s |

**Jump Lurch:**

| Constant | Value | Notes |
|---|---|---|
| `k_jumpLurchGraceMin` | 0.2 s | Full strength until this time |
| `k_jumpLurchGraceMax` | 0.5 s | Zero strength after this time |
| `k_jumpLurchStrength` | 5.0 | Multiplier (TF2 canonical value) |
| `k_jumpLurchMax` | 180 u/s | Velocity magnitude cap |
| `k_jumpLurchBaseVelocity` | 60 u/s | Base value before scaling |
| `k_jumpLurchSpeedLoss` | 0.125 | 12.5% speed penalty on lurch |

**Sliding:**

| Constant | Value | Notes |
|---|---|---|
| `k_slideMinStartSpeed` | 400 u/s | Must be moving this fast to slide |
| `k_slideMinSpeed` | 100 u/s | Auto-cancel below this |
| `k_slideBoostMin` | 80 u/s | Min entry speed boost |
| `k_slideBoostMax` | 200 u/s | Max entry speed boost |
| `k_slideBoostCooldown` | 2.0 s | Between slide boosts |
| `k_slideBrakingDecelMin` | 200 u/s^2 | Initial braking |
| `k_slideBrakingDecelMax` | 400 u/s^2 | Max braking (ramps up) |
| `k_slideBrakingRampTime` | 3.0 s | Time to reach max braking |
| `k_slideFloorInfluenceForce` | 400 u/s^2 | Slope influence on slide |
| `k_slideFatigueDecayTicks` | 384 | 3s at 128Hz to decay one fatigue level |
| `k_slideFatigueMax` | 4 | Max fatigue levels |

**Wallrunning:**

| Constant | Value | Notes |
|---|---|---|
| `k_wallrunCheckDist` | 35 u | Sphere-cast distance |
| `k_wallrunSphereRadius` | 12 u | Sphere-cast radius |
| `k_wallrunMinGroundDist` | 50 u | Min height to wallrun |
| `k_wallrunMaxSpeed` | 630 u/s | Max speed on wall |
| `k_wallrunAccel` | 800 u/s^2 | Forward acceleration |
| `k_wallrunPushForce` | 300 u/s^2 | Push toward wall |
| `k_wallrunKickoffDuration` | 1.75 s | Max time on same wall |
| `k_wallrunSpeedLossDelay` | 0.1 s | Wallkick tech window |
| `k_wallJumpUpForce` | 320 u/s | Wall jump upward |
| `k_wallJumpSideForce` | 350 u/s | Wall jump sideways |
| `k_wallrunExitTime` | 0.2 s | Exit-wall flag duration |
| `k_wallrunCameraTilt` | 7.5 deg | Camera roll on wall |
| `k_wallrunCameraTiltSpeed` | 10.0 | Tilt interpolation speed |

**Climbing:**

| Constant | Value | Notes |
|---|---|---|
| `k_climbCheckDist` | 35 u | Forward sphere-cast distance |
| `k_climbSphereRadius` | 12 u | Sphere-cast radius |
| `k_climbMaxSpeed` | 280 u/s | Starting climb speed |
| `k_climbMinSpeed` | 180 u/s | Decayed climb speed |
| `k_climbKickoffDuration` | 1.5 s | Max climb time |
| `k_climbMaxWallLookAngle` | 30 deg | Must face wall within this |
| `k_climbSidewaysMultiplier` | 0.1 | 10% lateral movement |
| `k_climbJumpUpForce` | 320 u/s | Climb jump upward |
| `k_climbJumpBackForce` | 350 u/s | Climb jump backward |
| `k_climbMinGroundDist` | 40 u | Min height to climb |
| `k_climbExitTime` | 0.5 s | Exit-climb flag duration |
| `k_climbRegrabLowerHeight` | 400 u | Must be this much lower to reclimb |

**Ledge Grabbing:**

| Constant | Value | Notes |
|---|---|---|
| `k_ledgeCheckDist` | 35 u | Forward trace distance |
| `k_ledgeSphereRadius` | 12 u | Sphere-cast radius |
| `k_ledgeMaxGrabDist` | 35 u | Max distance to grab |
| `k_ledgeMinHoldTime` | 0.5 s | Locked to ledge for this long |
| `k_ledgeMoveAccel` | 800 u/s^2 | Pull toward ledge |
| `k_ledgeMaxSpeed` | 400 u/s | Max pull speed |
| `k_ledgeJumpUpForce` | 380 u/s | Mantle upward |
| `k_ledgeJumpBackForce` | 120 u/s | Mantle push from wall |
| `k_ledgeExitTime` | 0.5 s | Exit-ledge flag duration |

**Grappling Hook:**

| Constant | Value | Notes |
|---|---|---|
| `k_grappleMaxRange` | 1500 u | Max hook reach |
| `k_grapplePullSpeed` | 900 u/s | Target pull speed |
| `k_grapplePullAccel` | 1800 u/s^2 | Acceleration toward hook |
| `k_grappleLookInfluence` | 0.35 | How much look direction steers the pull (0-1) |
| `k_grappleReleaseMinDist` | 60 u | Auto-release when close |
| `k_grappleReleaseMaxDist` | 2000 u | Auto-release when far |
| `k_grappleMaxDuration` | 7.0 s | Safety timeout |
| `k_grappleCooldown` | 1.0 s | Between grapple uses |
| `k_grappleGravityScale` | 0.15 | Gravity multiplier while grappling |
| `k_grappleSpeedCap` | 1500 u/s | Raised speed cap during grapple |

**Player Dimensions:**

| Constant | Value | Full Size |
|---|---|---|
| `k_standingHalfHeight` | 36 u | 72u tall (width 32u from CollisionShape) |
| `k_crouchingHalfHeight` | 22 u | 44u tall |

**Speed Cap:**

| Constant | Value | Notes |
|---|---|---|
| `k_speedCap` | 1200 u/s | Normal horizontal limit |

### 8.3 Tuning Notes

- **Gravity + Jump Speed** must be tuned as a pair. Changing one without the
  other breaks jump height: `apex = jumpSpeed^2 / (2 * gravity)`.

- **Air acceleration** is set to 5.0 -- a compromise between Quake/Source
  (0.7-10) and TMS (2.0). Higher values give more air control but make it
  easier to gain speed via strafing. The current value allows meaningful
  strafe-jumping without trivializing ground movement.

- **Slide braking ramp** exists to prevent infinite slides. The initial low
  deceleration gives a satisfying burst, while the increasing braking ensures
  slides always end.

- **Wallrun speed loss delay** (0.1s) is deliberately tiny -- just enough for
  a skilled player to wall-kick before speed is clamped. This creates a
  high-skill technique where entering a wallrun above max speed and
  immediately jumping preserves momentum.

---

## 9. Client-Server Parity

**Rule:** The movement system and collision system must produce
byte-identical results on client and server. Any divergence causes prediction
mismatches, rubber-banding, and desyncs.

### What Must Match

- `MovementSystem.cpp` / `.hpp` -- compiled identically on both sides.
- `CollisionSystem.cpp` / `.hpp` -- compiled identically on both sides.
- All `physics/` headers -- `Movement.hpp`, `SweptCollision.hpp`,
  `WallDetection.hpp`, `PhysicsConstants.hpp`, `TitanfallConstants.hpp`,
  `WorldData.hpp`.
- Component types -- `PlayerState`, `InputSnapshot`, `CollisionShape`,
  `Position`, `Velocity`.

### Architecture Decisions for Parity

1. **Pure-math physics layer:** No side effects, no state hidden in static
   variables (except the world geometry itself, which is const). Functions
   take values and return values.

2. **Shared source files:** Both client and server `#include` the same
   headers and compile the same `.cpp` files. There is no "client physics"
   vs "server physics" -- there is one physics.

3. **Static world geometry:** `testWorld()` returns a reference to static
   const data. Both sides call it and get identical geometry. When the game
   ships with dynamic maps, the map loader must produce identical
   `WorldGeometry` on both sides.

4. **Fixed timestep:** Both sides run physics at the same fixed `dt`
   (128 Hz). The client predicts locally using the same `dt` and replays
   corrections when the server's authoritative state disagrees.

5. **Deterministic inputs:** `InputSnapshot` captures absolute yaw/pitch
   (not deltas), WASD key state, and tick number. The server replays the
   exact same inputs the client used, producing the same result.

### What Can Differ (Safely)

- Rendering (interpolation, camera effects) -- client-only.
- Audio, particles, HUD -- client-only.
- Network serialization -- both sides agree on format, but the code is
  structurally different (client sends, server receives).

### Common Parity Pitfalls

- **Floating-point ordering:** Ensure both sides iterate geometry in the same
  order. The static arrays in `testWorld()` guarantee this.
- **Compiler flags:** Both client and server must use the same optimization
  level and FP model. Different `-ffast-math` settings can break
  determinism.
- **Platform differences:** If client is x86 and server is ARM, FP results
  may differ. Test cross-platform parity explicitly.
