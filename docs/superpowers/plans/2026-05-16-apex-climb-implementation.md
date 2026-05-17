# Apex-Style Climb Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current simple climb elevator with an Apex-inspired surface attachment system covering attach, climb space, upward/non-upward climb, slip/downward/sideways motion, detach penalties, end boost, reattach, and wallrun interplay.

**Architecture:** `MovementSystem` remains the state owner for traversal intent and velocity shaping. `WallDetection` remains the mesh query source, but climb needs a stable climb attachment query equivalent in quality to wallrun attachment. Keep the first implementation deterministic and server-authoritative; animation/camera polish follows after movement state is correct.

**Tech Stack:** C++23, EnTT, GLM, `physics_trimesh_tests`, authored static trimesh collision.

---

## Reference

- Feature set: `docs/apex-climb-wanted-feature-set.md`
- Source mechanics: https://apexmovement.tech/wiki/tech/General%20Tech%3EWall%20Tech%3EClimb%20Fundamentals%3EClimb%20Fundamentals

## Files

- Modify: `src/ecs/components/PlayerSimState.hpp`
- Modify: `src/ecs/components/PlayerVisState.hpp` if climb zone/debug visibility is needed by client animation or HUD.
- Modify: `src/ecs/physics/TitanfallConstants.hpp`
- Modify: `src/ecs/physics/WallDetection.hpp`
- Modify: `src/ecs/physics/WallDetection.cpp`
- Modify: `src/ecs/systems/MovementSystem.cpp`
- Modify: `src/ecs/physics/PhaseDiagnostic.hpp`
- Modify: `src/ecs/physics/PhaseDiagnostic.cpp`
- Modify: `tests/physics_trimesh_tests.cpp`

---

### Task 1: Climb Space State

**Files:**
- Modify: `src/ecs/components/PlayerSimState.hpp`
- Modify: `src/ecs/physics/TitanfallConstants.hpp`
- Modify: `tests/physics_trimesh_tests.cpp`

- [ ] **Step 1: Write failing state/default tests**

Add a test near `climbSameWallReattachRequiresMeaningfulDrop()`:

```cpp
bool climbSpaceDefaultsAreDeterministic()
{
    PlayerSimState state;
    bool ok = true;
    ok &= expect(state.climbAttachHeight == 0.0f, "climb attach height should default deterministically");
    ok &= expect(state.climbBaseline == 0.0f, "climb baseline should default deterministically");
    ok &= expect(state.climbSpaceCutoff == 0.0f, "climb cutoff should default deterministically");
    ok &= expect(state.climbPreviousAttachHeight <= -1e9f, "previous attach height should default to none");
    ok &= expect(!state.climbEndBoostQueued, "end boost should not default active");
    return ok;
}
```

Add it to `main()`.

- [ ] **Step 2: Verify red**

Run:

```bash
cmake --build build/debug --target physics_trimesh_tests -j 4
./build/debug/physics_trimesh_tests
```

Expected: compile failure for missing fields.

- [ ] **Step 3: Add state and constants**

Add to `PlayerSimState`:

```cpp
float climbBaseline{0.0f};
float climbSpaceCutoff{0.0f};
float climbAttachOffsetLimit{0.0f};
glm::vec3 climbPreviousWallNormal{0.0f};
float climbPreviousAttachHeight{-1e10f};
float climbDetachPenalty{0.0f};
bool climbEndBoostQueued{false};
```

Add to `TitanfallConstants.hpp`:

```cpp
constexpr float k_climbLookAngleLimit = 45.572995f;
constexpr float k_climbSurfaceMinDotUp = 0.7f;
constexpr float k_climbSpaceHeight = 147.0f;
constexpr float k_climbAttachOffset = 100.0f;
constexpr float k_climbMiniZoneHeight = 19.0f;
constexpr float k_climbGreenZoneHeight = 28.0f;
constexpr float k_climbReattachDifferentWallAngle = 25.842f;
constexpr float k_climbNormalDetachPenalty = 128.0f;
constexpr float k_climbJumpDetachPenalty = 256.0f;
constexpr float k_climbEndBoostUp = 28.0f;
```

- [ ] **Step 4: Verify green**

Run the same test command. Expected: pass.

- [ ] **Step 5: Commit**

```bash
git add src/ecs/components/PlayerSimState.hpp src/ecs/physics/TitanfallConstants.hpp tests/physics_trimesh_tests.cpp
git commit -m "Add climb space state"
```

### Task 2: Stable Climb Attachment Query

**Files:**
- Modify: `src/ecs/physics/WallDetection.hpp`
- Modify: `src/ecs/physics/WallDetection.cpp`
- Modify: `tests/physics_trimesh_tests.cpp`

- [ ] **Step 1: Write failing tests**

Add tests for:

```cpp
bool climbAttachAllowsMomentumStickWithoutForward();
bool climbAttachRejectsShallowSurface();
bool climbAttachRejectsExcessLookAngle();
```

The first test should give the player airborne velocity into a wall with no forward input and expect `MoveMode::Climbing` for at least one tick.

- [ ] **Step 2: Verify red**

Expected: no-forward momentum attach fails with the current `tryEnterClimb` intent gate.

- [ ] **Step 3: Add query result**

Extend `WallDetectionResult` or add `ClimbAttachmentResult` with:

```cpp
bool found{false};
glm::vec3 point{0.0f};
glm::vec3 normal{0.0f};
uint32_t meshIndex{UINT32_MAX};
uint32_t triId{UINT32_MAX};
physics::TriRegion region{physics::TriRegion::Face};
```

Use capsule/segment closest-point queries against trimeshes, not sphere-vs-AABB approximations.

- [ ] **Step 4: Update attach logic**

Attach if:

```cpp
const bool airborne = !state.vis.grounded;
const bool surfaceSteepEnough = std::abs(glm::dot(wallNormal, localUp)) < tms::k_climbSurfaceMinDotUp;
const bool lookValid = lookAngle <= glm::radians(tms::k_climbLookAngleLimit);
const bool hasInputIntent = glm::dot(wishDir, -wallNormal) >= tms::k_climbIntentThreshold;
const bool hasMomentumIntent = glm::dot(horizVel(vel), -wallNormal) > 25.0f;
const bool canStick = hasInputIntent || hasMomentumIntent;
```

- [ ] **Step 5: Verify green and commit**

Run `physics_trimesh_tests`, then commit:

```bash
git add src/ecs/physics/WallDetection.hpp src/ecs/physics/WallDetection.cpp src/ecs/systems/MovementSystem.cpp tests/physics_trimesh_tests.cpp
git commit -m "Add stable climb attachment query"
```

### Task 3: Upward, Slip, Downward, And Sideways Climb

**Files:**
- Modify: `src/ecs/systems/MovementSystem.cpp`
- Modify: `src/ecs/physics/TitanfallConstants.hpp`
- Modify: `tests/physics_trimesh_tests.cpp`

- [ ] **Step 1: Write failing behavior tests**

Add tests:

```cpp
bool upwardClimbTimerDoesNotExpireWhileMovingUp();
bool nonUpwardClimbExpiresAfterOneSecond();
bool slipMovesLocalDownBeforeTimerExpiry();
bool downwardClimbIsFasterThanPassiveSlip();
bool sidewaysClimbPreservesWallTangentSpeed();
```

- [ ] **Step 2: Verify red**

Expected: current implementation uses a short placeholder non-up timer, has no downward climb, and damps tangent velocity too aggressively.

- [ ] **Step 3: Implement climb sub-mode selection**

Inside `handleClimbing` compute:

```cpp
const glm::vec3 localUp{0.0f, state.vis.gravityFlipped ? -1.0f : 1.0f, 0.0f};
const float upSpeed = glm::dot(vel, localUp);
const glm::vec3 wallNormal = normalizedOrZero(state.sim.climbWallNormal);
const glm::vec3 wallTangentVelocity = vel - wallNormal * glm::dot(vel, wallNormal);
const glm::vec3 wishDir = physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);
const float wallIntent = glm::dot(wishDir, -wallNormal);
```

Rules:

- `wallIntent > threshold`: upward climb force.
- `wallIntent < -threshold`: downward climb force.
- mostly sideways input: tangent acceleration, non-up timer.
- no input: slip local-down, non-up timer.
- upSpeed above threshold: reset/ignore non-up expiry.

- [ ] **Step 4: Verify green and commit**

```bash
cmake --build build/debug --target physics_trimesh_tests -j 4
./build/debug/physics_trimesh_tests
git add src/ecs/systems/MovementSystem.cpp src/ecs/physics/TitanfallConstants.hpp tests/physics_trimesh_tests.cpp
git commit -m "Implement Apex-style climb submodes"
```

### Task 4: Climb Zones, Detach Penalties, And End Boost

**Files:**
- Modify: `src/ecs/systems/MovementSystem.cpp`
- Modify: `src/ecs/physics/PhaseDiagnostic.hpp`
- Modify: `src/ecs/physics/PhaseDiagnostic.cpp`
- Modify: `tests/physics_trimesh_tests.cpp`

- [ ] **Step 1: Write failing tests**

Add tests:

```cpp
bool miniZoneJumpGivesSmallWallPush();
bool greenZoneJumpGivesHigherWallbounceImpulse();
bool neutralZoneJumpGivesMostlyHorizontalWallPush();
bool climbSpaceCutoffTriggersEndBoost();
bool mantleDetachDoesNotApplyClimbSpacePenalty();
```

- [ ] **Step 2: Verify red**

Expected: zone-specific jump outputs and end boost do not exist.

- [ ] **Step 3: Implement zones**

Compute zone from local-up position:

```cpp
const float climbHeight = glm::dot(pos - state.sim.climbAttachPoint, localUp);
```

Classify mini/green/neutral/cutoff from constants. Store in diagnostics.

- [ ] **Step 4: Implement detach penalties and end boost**

Normal detaches lower climb baseline. Jump detaches use zone-specific penalty. Mantle transitions skip penalty. End boost queues a local-up impulse when attach offset or cutoff is crossed.

- [ ] **Step 5: Verify green and commit**

Run tests and commit:

```bash
git add src/ecs/systems/MovementSystem.cpp src/ecs/physics/PhaseDiagnostic.hpp src/ecs/physics/PhaseDiagnostic.cpp tests/physics_trimesh_tests.cpp
git commit -m "Add climb zones and end boost"
```

### Task 5: Reattach Rules

**Files:**
- Modify: `src/ecs/systems/MovementSystem.cpp`
- Modify: `tests/physics_trimesh_tests.cpp`

- [ ] **Step 1: Write failing tests**

Add tests:

```cpp
bool sameWallReattachRequiresDroppingBelowAttachPoint();
bool differentWallReattachAllowedAbovePreviousAttachByAngle();
bool reattachStillRequiresValidClimbSpace();
```

- [ ] **Step 2: Verify red**

Expected: current same-wall check is height-only and does not combine attach point with climb-space validity.

- [ ] **Step 3: Implement reattach gate**

Allow attach if:

```cpp
const bool lowerThanPreviousAttach = currentAttachHeight < state.sim.climbPreviousAttachHeight;
const bool differentEnoughWall =
    std::acos(std::clamp(glm::dot(newNormal, state.sim.climbPreviousWallNormal), -1.0f, 1.0f)) >=
    glm::radians(tms::k_climbReattachDifferentWallAngle);
const bool attachPointValid = lowerThanPreviousAttach || differentEnoughWall;
const bool climbSpaceValid = currentAttachHeight >= state.sim.climbBaseline &&
                             currentAttachHeight <= state.sim.climbSpaceCutoff;
```

- [ ] **Step 4: Verify green and commit**

```bash
cmake --build build/debug --target physics_trimesh_tests -j 4
./build/debug/physics_trimesh_tests
git add src/ecs/systems/MovementSystem.cpp tests/physics_trimesh_tests.cpp
git commit -m "Implement Apex-style climb reattach rules"
```

### Task 6: Wallrun And Climb Interplay

**Files:**
- Modify: `src/ecs/systems/MovementSystem.cpp`
- Modify: `tests/physics_trimesh_tests.cpp`

- [ ] **Step 1: Write failing tests**

Add tests:

```cpp
bool wallrunIntoVerticalBlockCanBecomeClimbStick();
bool sidewaysClimbCanTransitionIntoWallrun();
bool wallrunReleaseStillOverridesAccidentalClimb();
bool wallrunDoorGapDropsInsteadOfReverseOrAirAttach();
```

- [ ] **Step 2: Verify red**

Expected: at least wallrun-to-climb and sideways-climb-to-wallrun fail.

- [ ] **Step 3: Implement transition priority**

Priority should be:

1. jump release / explicit wallrun kick;
2. mantle/ledge;
3. valid wallrun continuation;
4. wallrun-to-climb if forward obstacle is climbable and jump is held;
5. drop.

- [ ] **Step 4: Verify green and commit**

```bash
cmake --build build/debug --target physics_trimesh_tests -j 4
./build/debug/physics_trimesh_tests
git add src/ecs/systems/MovementSystem.cpp tests/physics_trimesh_tests.cpp
git commit -m "Connect climb and wallrun transitions"
```

### Final Verification

- [ ] Run:

```bash
./build/debug/physics_trimesh_tests
cmake --build build/debug --target group2 server -j 4
PATH="/tmp/group2-clang-format-18:$PATH" git ls-files '*.cpp' '*.hpp' '*.h' '*.cc' '*.cxx' '*.c' '*.hh' '*.ipp' | PATH="/tmp/group2-clang-format-18:$PATH" xargs clang-format-18 --dry-run --Werror
```

- [ ] Playtest:
  - climb straight up a wall;
  - delayed forward stick;
  - slip and downward climb;
  - sideways climb;
  - wall push around doors;
  - wallrun into climb;
  - climb into wallrun;
  - repeated in-air reattach.

- [ ] Commit final fixes.

