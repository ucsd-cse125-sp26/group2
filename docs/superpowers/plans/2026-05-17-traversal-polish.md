# Traversal Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make climb/ledge behavior gravity-invariant and make wallrunning continue only through forward-compatible outside corners while dropping cleanly at gaps, doors, and dead ends.

**Architecture:** Keep `MovementSystem` as the owner of traversal state and velocity shaping, and keep `WallDetection`/`TriMeshCollision` as the owner of mesh queries. Add regression coverage in `tests/physics_trimesh_tests.cpp` before production edits. The implementation should stay deterministic and avoid animation/camera-only smoothing as a substitute for solver state.

**Tech Stack:** C++20, EnTT ECS, GLM, custom static-trimesh collision/KCC, `physics_trimesh_tests`.

---

### Task 1: Gravity-Invariant Climb And Ledge

**Files:**
- Modify: `tests/physics_trimesh_tests.cpp`
- Modify: `src/ecs/systems/MovementSystem.cpp`

- [x] **Step 1: Write failing tests**

Add tests that set `PlayerVisState::gravityFlipped = true` during `MoveMode::Climbing` and `MoveMode::LedgeGrabbing`.

Expected red behavior before the fix:
- Climbing sets positive `vel.y`, which is wrong when local up is `-Y`.
- Ledge auto-mantle sets positive `vel.y`, which is wrong when local up is `-Y`.

- [x] **Step 2: Run red tests**

Run:

```bash
cmake --build build/debug --target physics_trimesh_tests -j 4
./build/debug/physics_trimesh_tests
```

Expected: FAIL with messages about flipped climb moving along local up and flipped ledge mantle pushing along local up.

- [x] **Step 3: Implement local-up movement**

Change climb and ledge velocity shaping to use:

```cpp
const float gravDir = state.vis.gravityFlipped ? -1.0f : 1.0f;
vel.y = climbOrMantleSpeed * gravDir;
```

Clamp climb entry velocity with the same local-up convention:

```cpp
const float upVel = vel.y * gravDir;
if (upVel < 0.0f)
    vel.y = 0.0f;
```

- [x] **Step 4: Verify green**

Run the same test command. Expected: PASS.

- [x] **Step 5: Commit**

```bash
git add tests/physics_trimesh_tests.cpp src/ecs/systems/MovementSystem.cpp
git commit -m "Fix inverted gravity climb and ledge movement"
```

### Task 2: Wallrun Forward-Compatible Handoff

**Files:**
- Modify: `tests/physics_trimesh_tests.cpp`
- Modify: `src/ecs/systems/MovementSystem.cpp`
- Modify if query scoring needs a lower-level fix: `src/ecs/physics/TriMeshCollision.cpp`

- [x] **Step 1: Write failing tests**

Add a simulation test for a wallrun approaching an authored outside 90-degree corner. The player starts on the first wall with `wallNormal = {-1,0,0}` and `wallForward = {0,0,1}`. The test expects the next movement tick to stay in `MoveMode::WallRunning`, rotate to the continuation wall normal `{0,0,-1}`, and keep forward speed non-negative against the redirected wall forward.

Add a simulation test for a wallrun approaching a door/gap/dead-end with no forward-compatible continuation. The test expects the next movement tick to leave wallrunning and preserve non-reversed horizontal velocity.

- [x] **Step 2: Run red tests**

Run:

```bash
cmake --build build/debug --target physics_trimesh_tests -j 4
./build/debug/physics_trimesh_tests
```

Expected: at least one new wallrun handoff/drop test fails before the production edit.

- [x] **Step 3: Implement candidate compatibility**

In `handleWallRunning`, accept a refreshed attachment only when the new surface can produce a forward-compatible tangent:

```cpp
const glm::vec3 redirected = redirectWallForward(oldForward, oldNormal, candidateNormal);
const bool forwardCompatible = glm::dot(redirected, oldForward) > 0.05f;
const bool notReversedVelocity = glm::dot(horizVel(vel), redirected) > -1.0f;
```

If no compatible candidate exists, call `exitWallrun()` and return. Do not flip `wallForward` 180 degrees to keep the player attached.

- [x] **Step 4: Prefer outside-corner candidates over stale wall candidates**

When a lookahead attachment is available, prefer a candidate whose normal turns by up to 90 degrees and whose redirected tangent remains forward-compatible. Reject candidates with a turn greater than `k_wallrunMaxFaceRedirect` or with a tangent opposite the stored travel direction.

- [x] **Step 5: Verify green**

Run:

```bash
cmake --build build/debug --target physics_trimesh_tests -j 4
./build/debug/physics_trimesh_tests
cmake --build build/debug --target group2 server -j 4
```

Expected: all pass with no compiler warnings.

- [x] **Step 6: Commit**

```bash
git add tests/physics_trimesh_tests.cpp src/ecs/systems/MovementSystem.cpp src/ecs/physics/TriMeshCollision.cpp
git commit -m "Stabilize wallrun corner handoff and gap detach"
```

### Task 3: Apex-Style Climb Foundation

**Files:**
- Modify: `src/ecs/components/PlayerSimState.hpp`
- Modify: `src/ecs/physics/TitanfallConstants.hpp`
- Modify: `src/ecs/systems/MovementSystem.cpp`
- Modify: `tests/physics_trimesh_tests.cpp`

- [x] **Step 1: Write failing tests**

Add tests for:
- brief stick on wall contact without forward input;
- upward climb while movement points into the wall;
- downward/slip behavior when movement points away or input is neutral;
- non-upward climb timer expiry;
- same-wall reattach blocked until the player drops below the previous attach height, but allowed on a sufficiently different wall normal.

- [x] **Step 2: Implement minimal state**

Add climb attach tracking:

```cpp
glm::vec3 climbAttachPoint{0.0f};
float climbAttachHeight{0.0f};
float climbNonUpTimer{0.0f};
bool climbHadUpwardMotion{false};
```

- [x] **Step 3: Implement phases**

Use Apex-inspired rules:
- attach requires airborne wall contact and wall-facing/movement-toward-wall intent;
- upward climb is timerless while local-up velocity is above threshold;
- non-upward climb detaches after a short timer;
- neutral or away-from-wall input slips/down-climbs instead of forcing upward climb;
- jump/crouch/back detach cleanly and preserve outgoing velocity.

- [x] **Step 4: Verify**

Run:

```bash
cmake --build build/debug --target physics_trimesh_tests group2 server -j 4
./build/debug/physics_trimesh_tests
```

Expected: all traversal tests pass with no warnings.

- [x] **Step 5: Commit**

```bash
git add src/ecs/components/PlayerSimState.hpp src/ecs/physics/TitanfallConstants.hpp src/ecs/systems/MovementSystem.cpp tests/physics_trimesh_tests.cpp
git commit -m "Add Apex-style climb phase foundation"
```

### Final Verification

- [x] Run `./build/debug/physics_trimesh_tests`.
- [x] Run `cmake --build build/debug --target group2 server -j 4`.
- [x] Run the CI format gate:

```bash
PATH="/tmp/group2-clang-format-18:$PATH" git ls-files '*.cpp' '*.hpp' '*.h' '*.cc' '*.cxx' '*.c' '*.hh' '*.ipp' | PATH="/tmp/group2-clang-format-18:$PATH" xargs clang-format-18 --dry-run --Werror
```

- [x] Commit any final fixes.
