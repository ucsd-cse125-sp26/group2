# Titanfall Movement System (TMS) — Complete Feature & Constants Reference

> Reverse-engineered from the TMS v1.1 documentation (Unreal Engine Blueprint asset by Yannick),
> product listing, and Titanfall 2 speedrunning community knowledge.
> All values converted to **Quake/Source engine units** (1 qu ≈ 1 inch ≈ 2.54 cm).
>
> **Conversion used:** `quake_units = unreal_units / 2.54`
> (UE uses 1 UU = 1 cm; Source/Quake uses 1 unit ≈ 1 inch)

---

## 1. Core Movement

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Walk Speed | ~600 UU/s (UE default) | ~236 qu/s | UE Character default MaxWalkSpeed |
| Sprint Speed | 1150 UU/s | **~453 qu/s** | `SprintingSpeed` |
| Crouch Speed | 400 UU/s | **~157 qu/s** | `CrouchingSpeed` |
| Speed Cap (global max) | 2500 UU/s | **~984 qu/s** | `SpeedCap` — can be exceeded by grapple |
| Gravity Scale | 1.17 | 1.17× | `DefaultGravityScale` — slightly heavier than default |

### Capsule Dimensions

| Parameter | UE Value | Quake Units |
|---|---|---|
| Standing Half Height | 88 UU | **~34.6 qu** (full height ~69.3 qu) |
| Crouching Half Height | 55 UU | **~21.7 qu** (full height ~43.3 qu) |

---

## 2. Jumping

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Jump Z Velocity | 575 UU/s | **~226 qu/s** | `DefaultJumpZVelocity` |
| Slidehop Jump Z Velocity | 430 UU/s | **~169 qu/s** | Reduced height during slides |
| Double Jump | enabled | — | Resets on landing, wallrun start, or climb start |

---

## 3. Jump Lurch (Tap-Strafe / Directional Change)

This is the Titanfall-signature mechanic: a brief window after jumping where pressing a *different* movement key than you held at jump triggers a directional velocity impulse, at the cost of some speed.

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Lurch Grace Period Min | 0.2 s | 0.2 s | Closer to this = stronger lurch |
| Lurch Grace Period Max | 0.5 s | 0.5 s | After this, lurch disabled entirely |
| Lurch Strength | 5.0 | 5.0 | Multiplier (matches TF2's value of 5) |
| Lurch Max | 450 UU/s | **~177 qu/s** | Clamp on launch magnitude |
| Lurch Base Velocity | 100 UU/s | **~39 qu/s** | Base value scaled by timing & strength |
| Lurch Speed Loss | 12.5% | 12.5% | Speed penalty for lurching |

**How it works:** On jump, store current WASD input. If within the grace window a *different* key is pressed, apply `LaunchVelocity = LurchVelocity × LurchStrength × timingFactor`, clamped by `LurchMax`. Then reduce speed by `LurchSpeedLoss%`.

---

## 4. Coyote Time

| Parameter | UE Value | Notes |
|---|---|---|
| Coyote Time Duration | 0.165 s | Scales with movement speed — faster = longer window |

Works for both ground jumps (walking off ledges) and wall jumps (leaving a wall). Implemented via `OnMovementModeChanged`.

---

## 5. Sliding & Slide-Hopping

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Slide Min Start Speed | 850 UU/s | **~335 qu/s** | Must be going this fast to initiate |
| Slide Min Speed (auto-cancel) | 225 UU/s | **~89 qu/s** | Below this, slide → crouch |
| Slide Speed Burst (min) | 100 UU/s | **~39 qu/s** | Minimum boost on slide entry |
| Slide Speed Burst (max) | 400 UU/s | **~157 qu/s** | Maximum boost on slide entry |
| Slide Braking Decel (min) | 375 UU/s² | **~148 qu/s²** | Starting deceleration |
| Slide Braking Decel (max) | 750 UU/s² | **~295 qu/s²** | Max decel (ramps up over time) |
| Slideboost Cooldown | 2.0 s | 2.0 s | Can't get another entry-boost for 2s (TF2 rule) |
| Slide Floor Influence | 850,000 UU | — | Force pulling slide direction toward slope |
| Ground Friction during slide | 0.0 | 0.0 | Zeroed out for momentum preservation |
| Jump Z during slide | 430 UU/s | ~169 qu/s | Lower jump to keep slidehops grounded |

**Slide Fatigue:** A counter tracks successive slidehops. Each landing-during-slide gives a diminishing speed burst, discouraging pure jump-spam. The counter resets gradually after sliding stops.

**Slide Physics:** Ground friction → 0, braking decel starts at min and ramps to max over slide duration. Force is applied along the character's forward direction, influenced by floor normal (slopes accelerate/decelerate you). Air decel is also reduced so slidehop chains preserve velocity but can't go indefinitely.

---

## 6. Wallrunning

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Wallrun Max Speed | 1600 UU/s | **~630 qu/s** | Accelerates toward this while on wall |
| Wallrun Acceleration | 20 m/s → cN | **~20 m/s** | Converted to centinewtons in UE |
| Wallrun Gravity | disabled | — | `bUseWallrunGravity = false` (no gravity on wall, like TF2) |
| Gravity Counter Accel | 10 m/s | 10 m/s | If gravity enabled, this counterforce reduces pull |
| Wallrun Kickoff Duration | 1.75 s | 1.75 s | Max time on one wall before forced off |
| Wall Check Distance | 75 UU | **~29.5 qu** | Line trace distance left/right |
| Min Ground Distance | 125 UU | **~49.2 qu** | Must be this high off ground to wallrun |
| Wall Jump Up Force | 460 UU/s | **~181 qu/s** | Vertical launch on wall jump |
| Wall Jump Side Force | 800 UU/s | **~315 qu/s** | Horizontal launch away from wall |
| Camera Tilt | 7.5° | 7.5° | Roll toward wall side |
| Camera Tilt Interp Speed | 10 | 10 | Interpolation rate |
| Speed Loss Delay | 0.1 s | 0.1 s | Grace period before clamping to max speed (enables wallkicks) |
| Exit Wall Time | 0.2 s | 0.2 s | `bExitingWall` stays true for this long |

**Wall Blacklist:** You cannot grab the same wall twice UNLESS you grab it at a lower height. Stored as a struct with: wall reference, height of last wallrun, regrabbability flag. Cleared on landing.

**Wallkick Tech:** For the first `WallrunSpeedLossDelay` seconds, velocity is NOT clamped to `WallrunMaxSpeed`. If you're faster, you can wallkick (jump off almost immediately) to preserve or gain momentum.

**Tag System:** Any actor/component tagged `"Unwallrunnable"` is excluded.

---

## 7. Climbing

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Climb Max Speed | 550 UU/s | **~217 qu/s** | Starting climb speed, decreases over time |
| Climb Min Speed | 350 UU/s | **~138 qu/s** | Lowest climb speed |
| Climbing Kickoff Duration | 1.5 s | 1.5 s | Max time on wall before kicked off |
| Climbing Check Distance | 75 UU | **~29.5 qu** | Sphere trace forward distance |
| Climbing Sphere Cast Radius | 25 UU | **~9.8 qu** | Prevents losing wall when looking over ledge |
| Max Wall Look Angle | 30° | 30° | Must face wall within this angle to climb |
| Sideways Movement Multiplier | 0.1 (10%) | 10% | Heavily restricts lateral movement |
| Climb Jump Up Force | 460 UU/s | **~181 qu/s** | Vertical launch off climb |
| Climb Jump Back Force | 800 UU/s | **~315 qu/s** | Horizontal launch away from wall |
| Exit Wall Time (climbing) | 0.5 s | 0.5 s | Cooldown before re-climbing |
| Regrab Lower Height | 800 UU | **~315 qu** | Must grab 800 UU lower to re-climb same wall |
| Min Ground Distance | 75 UU | **~29.5 qu** | Must be this high to start climbing |

**Climbing Blacklist:** Same concept as wallrun blacklist — can't re-grab same wall unless significantly lower.

---

## 8. Ledge Grabbing

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Ledgegrab Max Speed | 800 UU/s | **~315 qu/s** | Pull-toward-ledge speed |
| Move-to-Ledge Acceleration | 20 m/s | 20 m/s | Acceleration toward grab point |
| Max Ledgegrab Distance | 75 UU | **~29.5 qu** | Release if too far |
| Ledgegrab Check Distance | 75 UU | **~29.5 qu** | Trace distance for detection |
| Ledgegrab Sphere Cast Radius | 25 UU | **~9.8 qu** | Detection sphere |
| Min Time on Ledge | 0.5 s | 0.5 s | Locked to ledge for at least this long |
| Ledge Jump Up Force | 600 UU/s | **~236 qu/s** | Vertical launch off ledge |
| Ledge Jump Back Force | 200 UU/s | **~79 qu/s** | Horizontal push off ledge |
| Exit Ledge Time | 0.5 s | 0.5 s | Cooldown after releasing |

**Tag System:** Ledge actors must have tag `"Ledge"` to be grabbable (current implementation, author plans to make this dynamic).

**Behavior:** Player freezes on ledge with gravity set to 0. After `MinTimeOnLedge`, pressing any movement key releases. A force continuously pushes player toward the ledge contact point.

---

## 9. Source-Like Air Acceleration (Air Strafing)

| Parameter | Value | Notes |
|---|---|---|
| `SVAccelerate` | **2.0** | In TF2 this is 2; in HL2/CS it's 10. This is THE key value. |

**This is the Quake/Source `SV_AirAccelerate` algorithm.** The implementation uses local variables: `WishSpeed`, `WishVelocity`, `CurrentSpeed`, `AddSpeed`, `AccelerationSpeed`, `GroundedWishSpeed`.

The low `SVAccelerate = 2` means:
- Air strafing provides **air control** (curve through air) but does NOT rapidly build speed
- Speed building comes primarily from wallkicks, slidehops, and grapple — not raw airstrafing
- Tap-strafing (rapid W-taps) enables sharp mid-air turns via the lurch system, not the airstrafe itself

---

## 10. Grappling Hook

### Player Character Side

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Grapple Pull Speed | 1000 UU/s | **~394 qu/s** | How fast grapple pulls you |
| Grapple Pull Duration | 2.5 s | 2.5 s | Max active pull time |
| Grapple Cooldown | 1.0 s | 1.0 s | Between grapple uses |
| Gravity during grapple | 0.33× | 0.33× | Reduced from 1.17 to ~0.33 |

### Grapple Hook Actor

| Parameter | UE Value | Quake Units | Notes |
|---|---|---|---|
| Hook Throw Speed | 5300 UU/s | **~2087 qu/s** | Projectile travel speed |
| Hook Max Distance | 2100 UU | **~827 qu** | Max range before auto-destroy |
| Release Min Distance | 190 UU | **~75 qu** | Auto-release when this close to hook point |
| Release Max Distance | 3200 UU | **~1260 qu** | Auto-release when this far from hook point |
| Max Travel Time | 1.75 s | 1.75 s | Hook projectile lifetime |

**Grapple Physics:** The pull direction is a blend of the vector toward the hook point and the player's current look direction. Looking away from the hook point causes a swing effect while still being pulled closer. This is what makes it feel like Pathfinder's grapple — simultaneous pull + swing.

**Cancellation:** Re-pressing grapple key, or pressing crouch while being pulled.

**Tag System:** Actors/components tagged `"NoGrapple"` reject the hook.

---

## 11. Complete Feature Checklist

| # | Feature | Status | Skill Ceiling Impact |
|---|---|---|---|
| 1 | Walk / Sprint / Crouch | ✅ | Basic |
| 2 | Jumping (standard) | ✅ | Basic |
| 3 | Double Jump | ✅ | Basic |
| 4 | Coyote Time (ground + wall) | ✅ | Medium — forgiving ledge/wall jumps |
| 5 | Jump Lurch / Tap Strafing | ✅ | **High** — mid-air directional changes |
| 6 | Source-Style Air Acceleration | ✅ | **High** — air control curves |
| 7 | Sliding | ✅ | Medium — momentum entry |
| 8 | Slide-Hopping | ✅ | **High** — chained momentum preservation |
| 9 | Slide Fatigue (diminishing returns) | ✅ | Medium — prevents infinite slidehop spam |
| 10 | Slideboost Cooldown (2s) | ✅ | Medium — TF2-accurate restriction |
| 11 | Wallrunning (physics-based) | ✅ | **High** — core traversal mechanic |
| 12 | Wall Blacklist (no re-grab same wall higher) | ✅ | **High** — forces creative routing |
| 13 | Wallkick Tech (speed-loss delay) | ✅ | **Very High** — advanced speed tech |
| 14 | Wall Climbing | ✅ | Medium — vertical navigation |
| 15 | Climbing Speed Decay | ✅ | Medium — can't climb forever |
| 16 | Ledge Grabbing | ✅ | Medium — vertical traversal finisher |
| 17 | Ledge Hold + Timed Release | ✅ | Low-Medium |
| 18 | Grappling Hook (pull + swing) | ✅ | **Very High** — massive speed generation |
| 19 | Camera Tilt (wallrun) | ✅ | Polish — directional feedback |
| 20 | Headbobbing (walk/run/slide) | ✅ | Polish — kinesthetic feedback |
| 21 | Camera Shake (landing, etc.) | ✅ | Polish |
| 22 | Velocity Clamping (`SpeedCap`) | ✅ | Balance — prevents infinite speed |
| 23 | Multiplayer Replication | ✅ | Networking — server-side sprinting, velocity, gravity |
| 24 | `"Unwallrunnable"` Tag System | ✅ | Level design tool |
| 25 | `"Ledge"` Tag System | ✅ | Level design tool |
| 26 | `"NoGrapple"` Tag System | ✅ | Level design tool |

---

## 12. Quick-Reference: Quake Unit Constants for Your SDL3/C++ Implementation

```cpp
// ============================================================
// TMS-equivalent constants in Quake Units (1 qu ≈ 1 inch)
// For SDL3 + EnTT + C++ game
// ============================================================

// --- Core Movement ---
constexpr float WALK_SPEED         = 236.0f;   // qu/s
constexpr float SPRINT_SPEED       = 453.0f;   // qu/s
constexpr float CROUCH_SPEED       = 157.0f;   // qu/s
constexpr float SPEED_CAP          = 984.0f;   // qu/s (global max, grapple can exceed)
constexpr float GRAVITY_SCALE      = 1.17f;    // multiplier on default gravity

// --- Capsule ---
constexpr float STANDING_HALF_HEIGHT  = 34.6f;  // qu
constexpr float CROUCHING_HALF_HEIGHT = 21.7f;  // qu

// --- Jumping ---
constexpr float JUMP_VELOCITY       = 226.0f;  // qu/s upward
constexpr float SLIDEHOP_JUMP_VEL   = 169.0f;  // qu/s (reduced during slide)

// --- Jump Lurch ---
constexpr float LURCH_GRACE_MIN     = 0.2f;    // seconds
constexpr float LURCH_GRACE_MAX     = 0.5f;    // seconds
constexpr float LURCH_STRENGTH      = 5.0f;    // multiplier (TF2 canonical value)
constexpr float LURCH_MAX           = 177.0f;  // qu/s clamp
constexpr float LURCH_BASE_VEL      = 39.0f;   // qu/s
constexpr float LURCH_SPEED_LOSS    = 0.125f;  // 12.5%

// --- Coyote Time ---
constexpr float COYOTE_TIME         = 0.165f;  // seconds (scales with speed)

// --- Sliding ---
constexpr float SLIDE_MIN_START_SPEED = 335.0f; // qu/s to initiate
constexpr float SLIDE_MIN_SPEED       = 89.0f;  // qu/s auto-cancel threshold
constexpr float SLIDE_BURST_MIN       = 39.0f;  // qu/s entry boost min
constexpr float SLIDE_BURST_MAX       = 157.0f; // qu/s entry boost max
constexpr float SLIDE_DECEL_MIN       = 148.0f; // qu/s² initial braking
constexpr float SLIDE_DECEL_MAX       = 295.0f; // qu/s² max braking (ramps up)
constexpr float SLIDE_BOOST_COOLDOWN  = 2.0f;   // seconds between boosts
constexpr float SLIDE_GROUND_FRICTION = 0.0f;   // zero friction while sliding

// --- Wallrunning ---
constexpr float WALLRUN_MAX_SPEED     = 630.0f;  // qu/s
constexpr float WALLRUN_ACCEL         = 20.0f;   // m/s (apply as force)
constexpr float WALLRUN_KICKOFF_TIME  = 1.75f;   // seconds max on one wall
constexpr float WALL_CHECK_DIST       = 29.5f;   // qu (trace distance)
constexpr float WALL_MIN_GROUND_DIST  = 49.2f;   // qu
constexpr float WALL_JUMP_UP          = 181.0f;   // qu/s vertical
constexpr float WALL_JUMP_SIDE        = 315.0f;   // qu/s horizontal
constexpr float WALLRUN_CAM_TILT      = 7.5f;    // degrees
constexpr float WALLRUN_SPEED_LOSS_DELAY = 0.1f;  // seconds (wallkick tech window)
constexpr bool  WALLRUN_GRAVITY       = false;    // no gravity on wall (TF2 default)

// --- Climbing ---
constexpr float CLIMB_MAX_SPEED       = 217.0f;  // qu/s (decreases over time)
constexpr float CLIMB_MIN_SPEED       = 138.0f;  // qu/s
constexpr float CLIMB_KICKOFF_TIME    = 1.5f;    // seconds
constexpr float CLIMB_CHECK_DIST      = 29.5f;   // qu
constexpr float CLIMB_SPHERE_RADIUS   = 9.8f;    // qu
constexpr float CLIMB_MAX_LOOK_ANGLE  = 30.0f;   // degrees
constexpr float CLIMB_SIDE_MULTIPLIER = 0.1f;    // 10% lateral movement
constexpr float CLIMB_JUMP_UP         = 181.0f;  // qu/s
constexpr float CLIMB_JUMP_BACK       = 315.0f;  // qu/s
constexpr float CLIMB_REGRAB_LOWER    = 315.0f;  // qu (must grab this much lower)
constexpr float CLIMB_MIN_GROUND_DIST = 29.5f;   // qu

// --- Ledge Grab ---
constexpr float LEDGE_MAX_SPEED       = 315.0f;  // qu/s pull toward ledge
constexpr float LEDGE_ACCEL           = 20.0f;   // m/s
constexpr float LEDGE_MAX_DIST        = 29.5f;   // qu
constexpr float LEDGE_MIN_HOLD_TIME   = 0.5f;    // seconds locked to ledge
constexpr float LEDGE_JUMP_UP         = 236.0f;  // qu/s
constexpr float LEDGE_JUMP_BACK       = 79.0f;   // qu/s

// --- Air Acceleration (Source Engine style) ---
constexpr float SV_AIRACCELERATE      = 2.0f;    // THE key value. TF2=2, HL2/CS=10

// --- Grappling Hook ---
constexpr float GRAPPLE_PULL_SPEED    = 394.0f;   // qu/s
constexpr float GRAPPLE_PULL_DURATION = 2.5f;     // seconds
constexpr float GRAPPLE_COOLDOWN      = 1.0f;     // seconds
constexpr float GRAPPLE_GRAVITY       = 0.33f;    // gravity multiplier while grappling
constexpr float HOOK_THROW_SPEED      = 2087.0f;  // qu/s projectile
constexpr float HOOK_MAX_RANGE        = 827.0f;   // qu
constexpr float HOOK_RELEASE_MIN      = 75.0f;    // qu (auto-release close)
constexpr float HOOK_RELEASE_MAX      = 1260.0f;  // qu (auto-release far)
constexpr float HOOK_MAX_TRAVEL_TIME  = 1.75f;    // seconds
```

---

## 13. Implementation Notes for SDL3 + C++

### Air Acceleration Algorithm
The core of the Source-style air control is the `SV_AirAccelerate` function. Pseudocode:

```cpp
void AirAccelerate(Vec3& velocity, const Vec3& wishDir, float wishSpeed, float accel, float dt) {
    float currentSpeed = dot(velocity, wishDir);
    float addSpeed = wishSpeed - currentSpeed;
    if (addSpeed <= 0) return;
    
    float accelSpeed = accel * wishSpeed * dt;
    if (accelSpeed > addSpeed) accelSpeed = addSpeed;
    
    velocity += wishDir * accelSpeed;
}
// Call with accel = SV_AIRACCELERATE (2.0 for TF2 feel)
```

### Slide Physics
- On slide start: zero ground friction, reduce braking decel to min, reduce jump height
- Each tick: apply forward force influenced by floor normal, ramp braking decel from min→max over time
- Slidehop: on landing-during-slide, give diminishing speed burst (fatigue counter)

### Wallrun Physics
- Each tick while wallrunning:
  1. Apply force along wall-forward direction (cross product of wall normal and up)
  2. Push player toward wall (inverted normal × large force)
  3. After `WALLRUN_SPEED_LOSS_DELAY`, clamp velocity to `WALLRUN_MAX_SPEED`
  4. If `WALLRUN_GRAVITY == false`, set gravity to 0; else apply counter-force

### Grapple Physics
- Pull direction = `normalize(hookPoint - playerPos) + playerLookDir`
- This blend creates the simultaneous pull + swing characteristic
- Reduce gravity to 0.33× while grappling

---

*Generated from TMS v1.1 documentation and Titanfall 2 community research.*
*For your SDL3 + EnTT + C++ game project.*
