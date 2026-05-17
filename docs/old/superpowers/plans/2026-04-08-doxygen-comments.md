# Doxygen Comments Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate all source comments to Google-style Doxygen (`///`) and wire up a CMake `docs` target that generates an HTML reference site.

**Architecture:** All public API declarations in `.hpp` files get `///` Doxygen doc-comments with `@brief`/`@param`/`@return`. Private implementation-only functions in `.cpp` files get a short `/// @brief` instead of `// ----` section boxes. Algorithm commentary and phase markers inside function bodies stay as plain `//` — they are not API docs. A `Doxyfile.in` + `cmake/Doxygen.cmake` module wires Doxygen into the build.

**Tech Stack:** C++23, CMake 3.25+, Doxygen 1.9+

**Comment style rules (apply uniformly):**
- `///` prefix for every Doxygen doc-comment (never `/** */`)
- `/// @brief` — first (brief) sentence on public declarations
- `/// @param name Desc.` — one per parameter on non-obvious functions
- `/// @return Desc.` — when the return value needs explanation
- `/// @pre Desc.` — preconditions (e.g. "must be called after init()")
- `/// @note Desc.` — important gotchas or design decisions
- `///<` trailing comment — struct/class member documentation
- Plain `//` inside function bodies — algorithm steps, phase markers, inline math

---

## File Map

| File | Action |
|---|---|
| `cmake/Doxygen.cmake` | **Create** — CMake helper that finds Doxygen and creates the `docs` target |
| `Doxyfile.in` | **Create** — Doxygen configuration template |
| `CMakeLists.txt` | **Modify** — include cmake/Doxygen.cmake |
| `src/ecs/components/Position.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/components/Velocity.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/components/CollisionShape.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/components/PlayerState.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/components/InputSnapshot.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/components/PreviousPosition.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/components/LocalPlayer.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/registry/Registry.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/physics/PhysicsConstants.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/physics/Movement.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/physics/SweptCollision.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/physics/Movement.cpp` | **Modify** — plain `//` stays, no section boxes exist |
| `src/ecs/physics/SweptCollision.cpp` | **Modify** — plain `//` stays, no section boxes exist |
| `src/ecs/systems/Systems.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/systems/MovementSystem.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/systems/MovementSystem.cpp` | **Modify** — replace `// ---` section dividers with simple labels |
| `src/ecs/systems/CollisionSystem.hpp` | **Modify** — convert to Doxygen |
| `src/ecs/systems/CollisionSystem.cpp` | **Modify** — replace `// ----` boxes on private functions with `/// @brief` |
| `src/client/game/Game.hpp` | **Modify** — convert to Doxygen |
| `src/client/game/Game.cpp` | **Modify** — replace `// --- Section ---` dividers with simple `//` labels |
| `src/client/network/Client.hpp` | **Modify** — convert to Doxygen |
| `src/client/network/Client.cpp` | **Modify** — no doc changes needed (no section boxes) |
| `src/client/renderer/Renderer.hpp` | **Modify** — convert to Doxygen |
| `src/client/renderer/Renderer.cpp` | **Modify** — replace `// ----` boxes with `/// @brief` on private helpers |
| `src/client/debug/DebugUI.hpp` | **Modify** — convert to Doxygen |
| `src/client/debug/DebugUI.cpp` | **Modify** — replace `// ----` section dividers inside functions with plain `//` |
| `src/client/systems/InputSampleSystem.hpp` | **Modify** — convert to Doxygen |
| `src/client/systems/PredictionSystem.hpp` | **Modify** — replace IDE stub with `#pragma once` + Doxygen stub |
| `src/client/systems/InputSendSystem.hpp` | **Modify** — replace IDE stub with `#pragma once` + Doxygen stub |
| `src/client/systems/ReconciliationSystem.hpp` | **Modify** — replace IDE stub with `#pragma once` + Doxygen stub |
| `src/server/game/ServerGame.hpp` | **Modify** — convert to Doxygen |
| `src/server/game/ServerGame.cpp` | **Modify** — minor comment cleanups |
| `src/server/network/Server.hpp` | **Modify** — convert to Doxygen |
| `src/server/network/Server.cpp` | **Modify** — no doc changes needed |
| `src/server/systems/BroadcastSystem.hpp` | **Modify** — replace IDE stub with `#pragma once` + Doxygen stub |
| `src/server/systems/InputReceiveSystem.hpp` | **Modify** — replace IDE stub with `#pragma once` + Doxygen stub |

---

## Task 1: Doxygen Infrastructure

**Files:**
- Create: `Doxyfile.in`
- Create: `cmake/Doxygen.cmake`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `Doxyfile.in`**

```ini
# Doxyfile.in — filled in by CMake (cmake/Doxygen.cmake)
PROJECT_NAME        = "@PROJECT_NAME@"
PROJECT_BRIEF       = "@PROJECT_DESCRIPTION@"
PROJECT_NUMBER      = "@PROJECT_VERSION@"

INPUT               = "@CMAKE_SOURCE_DIR@/src"
RECURSIVE           = YES
FILE_PATTERNS       = *.cpp *.hpp *.h
EXCLUDE_PATTERNS    = */build/* */_deps/* */cmake-build-*/*

OUTPUT_DIRECTORY    = "@DOXYGEN_OUTPUT_DIR@"
HTML_OUTPUT         = html
GENERATE_HTML       = YES
GENERATE_LATEX      = NO

GENERATE_TREEVIEW   = YES
DISABLE_INDEX       = NO

# Extract everything so the site is complete even for undocumented items.
EXTRACT_ALL         = YES
EXTRACT_PRIVATE     = NO
EXTRACT_STATIC      = YES

# Brief description is the first sentence automatically.
JAVADOC_AUTOBRIEF   = YES
QT_AUTOBRIEF        = NO

# Keep noise low — we'll add warnings if the project matures.
WARN_IF_UNDOCUMENTED    = NO
WARN_IF_DOC_ERROR       = YES
WARN_NO_PARAMDOC        = NO

HAVE_DOT            = NO
QUIET               = NO
```

- [ ] **Step 2: Create `cmake/Doxygen.cmake`**

```cmake
# cmake/Doxygen.cmake
# Finds Doxygen and registers a `docs` build target.
# Include from CMakeLists.txt after the project() call.

find_package(Doxygen OPTIONAL_COMPONENTS dot)

if(DOXYGEN_FOUND)
    set(DOXYGEN_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")

    configure_file(
        "${CMAKE_SOURCE_DIR}/Doxyfile.in"
        "${CMAKE_BINARY_DIR}/Doxyfile"
        @ONLY
    )

    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )

    message(STATUS "Doxygen ${DOXYGEN_VERSION} found — run 'cmake --build . --target docs' to generate docs")
else()
    message(STATUS "Doxygen not found — 'docs' target unavailable. Install with: "
                   "sudo apt-get install doxygen  OR  brew install doxygen")
endif()
```

- [ ] **Step 3: Add `include(cmake/Doxygen.cmake)` to `CMakeLists.txt`**

In `CMakeLists.txt`, directly after the two existing `include(...)` lines (lines 18–19), add:

```cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxygen.cmake")
```

The block should then read:
```cmake
include(FetchContent)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sanitizers.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Doxygen.cmake")
```

- [ ] **Step 4: Verify CMake configure succeeds**

```bash
cmake --preset debug
```

Expected: configure output includes `Doxygen X.Y.Z found — run 'cmake --build . --target docs'...`
If Doxygen is not installed: `sudo apt-get install doxygen` then re-run.

- [ ] **Step 5: Verify the `docs` target runs (HTML not reviewed yet)**

```bash
cmake --build --preset debug --target docs
```

Expected: `Generating API documentation with Doxygen` + exit 0.
Docs land in `build/docs/html/index.html`.

- [ ] **Step 6: Commit**

```bash
git add Doxyfile.in cmake/Doxygen.cmake CMakeLists.txt
git commit -m "build: add Doxygen infrastructure (Doxyfile.in + cmake/Doxygen.cmake)"
```

---

## Task 2: ECS Component Headers

**Files:**
- Modify: `src/ecs/components/Position.hpp`
- Modify: `src/ecs/components/Velocity.hpp`
- Modify: `src/ecs/components/CollisionShape.hpp`
- Modify: `src/ecs/components/PlayerState.hpp`
- Modify: `src/ecs/components/InputSnapshot.hpp`
- Modify: `src/ecs/components/PreviousPosition.hpp`
- Modify: `src/ecs/components/LocalPlayer.hpp`

- [ ] **Step 1: Rewrite `src/ecs/components/Position.hpp`**

```cpp
#pragma once

#include <glm/vec3.hpp>

/// @brief World-space position of an entity, in game units.
struct Position
{
    glm::vec3 value{0.0f, 0.0f, 0.0f}; ///< XYZ position (Y-up, Quake units).
};
```

- [ ] **Step 2: Rewrite `src/ecs/components/Velocity.hpp`**

```cpp
#pragma once

#include <glm/vec3.hpp>

/// @brief Linear velocity of an entity, in game units per second.
struct Velocity
{
    glm::vec3 value{0.0f, 0.0f, 0.0f}; ///< XYZ velocity (Y-up, units/s).
};
```

- [ ] **Step 3: Rewrite `src/ecs/components/CollisionShape.hpp`**

```cpp
#pragma once

#include <glm/vec3.hpp>

/// @brief Axis-aligned bounding box defined by half-extents from the entity's Position.
///
/// The full bounding box spans `[pos - halfExtents, pos + halfExtents]`.
///
/// Default is a standing player in Quake-ish units:
/// - Width  = 32  (`halfExtents.x/z = 16`)
/// - Height = 72  (`halfExtents.y   = 36`)
struct CollisionShape
{
    glm::vec3 halfExtents{16.0f, 36.0f, 16.0f}; ///< AABB half-dimensions (units).
};
```

- [ ] **Step 4: Rewrite `src/ecs/components/PlayerState.hpp`**

```cpp
#pragma once

/// @brief Locomotion state flags for a player entity.
///
/// Read by MovementSystem to select the correct physics constants.
/// Written by CollisionSystem (`grounded`) and MovementSystem (`crouching`, `sliding`).
struct PlayerState
{
    bool grounded{false};  ///< True when touching a floor surface this tick.
    bool crouching{false}; ///< True when crouch input is held; CollisionShape.halfExtents.y is reduced.
    bool sliding{false};   ///< True when a momentum slide is active (crouch at speed).
};
```

- [ ] **Step 5: Rewrite `src/ecs/components/InputSnapshot.hpp`**

```cpp
#pragma once

#include <cstdint>

/// @brief One tick of player input, stamped with the tick it was sampled on.
///
/// Sent client → server each tick.
/// Stored in the client's ring buffer for prediction reconciliation.
///
/// `yaw` and `pitch` are absolute orientations in radians, not deltas.
/// The server needs the full orientation to compute `wishDir` correctly.
/// `pitch` is clamped to `[-89°, +89°]` (~1.5533 rad) by InputSampleSystem.
struct InputSnapshot
{
    uint32_t tick{0}; ///< Physics tick this snapshot was sampled on.

    // Movement keys
    bool forward{false}; ///< W key.
    bool back{false};    ///< S key.
    bool left{false};    ///< A key.
    bool right{false};   ///< D key.
    bool jump{false};    ///< Space key.
    bool crouch{false};  ///< Left Ctrl key.

    /// @brief Horizontal look angle in radians (accumulated from mouse X deltas).
    float yaw{0.0f};

    /// @brief Vertical look angle in radians, clamped to `[-89°, +89°]` by InputSampleSystem.
    float pitch{0.0f};

    /// @brief Currently always 0; reserved for dynamic movement tilt (wallrun lean, strafe tilt).
    float roll{0.0f};
};
```

- [ ] **Step 6: Rewrite `src/ecs/components/PreviousPosition.hpp`**

```cpp
#pragma once

#include <glm/vec3.hpp>

/// @brief Copy of Position from the previous physics tick, used for render interpolation.
///
/// Written by the game loop immediately before each physics step:
/// @code
///   registry.get<PreviousPosition>(e).value = registry.get<Position>(e).value;
/// @endcode
///
/// Read by the renderer to interpolate between ticks:
/// @code
///   renderPos = glm::mix(prev.value, cur.value, alpha);
/// @endcode
///
/// @note Only the client needs this component. The server has no renderer.
struct PreviousPosition
{
    glm::vec3 value{0.0f, 0.0f, 0.0f}; ///< World-space position from the previous tick.
};
```

- [ ] **Step 7: Rewrite `src/ecs/components/LocalPlayer.hpp`**

```cpp
#pragma once

/// @brief Marker component that tags exactly one entity per client as the locally controlled player.
///
/// Used by InputSampleSystem to distinguish the local player from remote player entities.
/// Remote entities also carry InputSnapshot for server-side simulation, but must never
/// be overwritten by local input.
struct LocalPlayer
{};
```

- [ ] **Step 8: Verify build still compiles**

```bash
cmake --build --preset debug
```

Expected: build succeeds, zero errors.

- [ ] **Step 9: Commit**

```bash
git add src/ecs/components/
git commit -m "docs: convert ECS component headers to Google Doxygen style"
```

---

## Task 3: Physics + Registry Headers

**Files:**
- Modify: `src/ecs/registry/Registry.hpp`
- Modify: `src/ecs/physics/PhysicsConstants.hpp`
- Modify: `src/ecs/physics/Movement.hpp`
- Modify: `src/ecs/physics/SweptCollision.hpp`

- [ ] **Step 1: Rewrite `src/ecs/registry/Registry.hpp`**

```cpp
#pragma once

#include <entt/entt.hpp>

/// @brief Shared ECS registry type alias.
///
/// Uses `entt::registry` directly. EnTT is a required dependency.
using Registry = entt::registry;
```

- [ ] **Step 2: Rewrite `src/ecs/physics/PhysicsConstants.hpp`**

```cpp
#pragma once

/// @brief All physics tuning values in one place.
///
/// **Units:** Quake units (1 unit ≈ 1 inch), Y-up coordinate system.
///
/// Starting values target a Titanfall-to-Quake movement feel.
/// Tune iteratively — `k_gravity` and `k_jumpSpeed` must always be tuned together:
/// `jump height = k_jumpSpeed² / (2 × k_gravity)`.
namespace physics
{

// Gravity & jumping
constexpr float k_gravity   = 1000.0f; ///< Downward acceleration (units/s²). Faster than real-world for snappy arcs.
constexpr float k_jumpSpeed = 380.0f;  ///< Initial upward velocity on jump (units/s). Gives apex ≈ 72 units (~6 ft).

// Ground movement
constexpr float k_maxGroundSpeed = 400.0f; ///< Maximum horizontal speed on ground (units/s).
constexpr float k_groundAccel    = 15.0f;  ///< Ground acceleration constant. Higher = reaches max speed faster.

// Air movement
/// @brief Air acceleration constant. Higher than Quake (0.7) for Titanfall-style air control.
constexpr float k_airAccel    = 2.0f;
/// @brief Wish-speed cap in air (units/s). Does NOT cap total speed — existing momentum is preserved.
constexpr float k_airMaxSpeed = 30.0f;

// Friction
constexpr float k_friction   = 4.0f;   ///< Ground friction coefficient (Quake default).
constexpr float k_stopSpeed  = 150.0f; ///< Friction is amplified below this speed for a crisp stop.

// Collision
constexpr float k_overbounceWall  = 1.001f; ///< Separation impulse for walls/ceilings; prevents corner-sticking.
constexpr float k_overbounceFloor = 1.0f;   ///< Floor overbounce — exactly 1.0 means no bounce.

// Geometry
constexpr float k_stepHeight = 18.0f; ///< Maximum obstacle height auto-stepped over without jumping (units).

} // namespace physics
```

- [ ] **Step 3: Rewrite `src/ecs/physics/Movement.hpp`**

```cpp
#pragma once

#include <glm/vec3.hpp>

/// @brief Pure physics math — no ECS types, no registry.
///
/// All functions take values in and return values out (no mutation via pointer).
/// Constants come from PhysicsConstants.hpp.
namespace physics
{

/// @brief Apply gravity for one tick: subtracts `k_gravity * dt` from the Y component.
/// @param vel  Current velocity.
/// @param dt   Delta time in seconds.
/// @return     New velocity with gravity applied.
/// @note       Call every tick when the entity is airborne (not grounded).
glm::vec3 applyGravity(glm::vec3 vel, float dt);

/// @brief Apply Quake-style ground friction to horizontal (XZ) velocity.
///
/// Uses `k_stopSpeed` as a minimum control speed so entities stop crisply
/// rather than asymptotically approaching zero.
///
/// @param vel  Current velocity.
/// @param dt   Delta time in seconds.
/// @return     New velocity with friction applied to XZ; Y is unchanged.
/// @note       Call every tick when the entity is grounded.
glm::vec3 applyGroundFriction(glm::vec3 vel, float dt);

/// @brief Quake PM_Accelerate: accelerate toward `wishDir` up to `wishSpeed`.
///
/// Does **not** cap total speed — only the projection of velocity onto `wishDir`
/// is capped at `wishSpeed`. Existing momentum in any other direction is untouched.
/// This property is what makes strafe jumping possible.
///
/// @param vel        Current velocity.
/// @param wishDir    Normalised desired movement direction (from InputSnapshot + yaw).
/// @param wishSpeed  Target speed (k_maxGroundSpeed on ground, k_airMaxSpeed in air).
/// @param accel      Acceleration constant (k_groundAccel or k_airAccel).
/// @param dt         Delta time in seconds.
/// @return           New velocity with acceleration applied.
glm::vec3 accelerate(glm::vec3 vel, glm::vec3 wishDir, float wishSpeed, float accel, float dt);

/// @brief Project velocity onto a collision surface to slide along it.
/// @param vel         Current velocity.
/// @param normal      Surface normal at the contact point.
/// @param overbounce  Separation scalar: use k_overbounceFloor for floors, k_overbounceWall for walls/ceilings.
/// @return            Clipped velocity that slides along the surface.
glm::vec3 clipVelocity(glm::vec3 vel, glm::vec3 normal, float overbounce);

/// @brief Compute the horizontal wish direction from yaw angle and WASD key state.
/// @param yaw      Player's current yaw in radians.
/// @param forward  True when W is held.
/// @param back     True when S is held.
/// @param left     True when A is held.
/// @param right    True when D is held.
/// @return         Normalised XZ direction vector, or `(0,0,0)` if no keys are pressed.
/// @note           Y component is always 0 — vertical movement is handled separately.
glm::vec3 computeWishDir(float yaw, bool forward, bool back, bool left, bool right);

} // namespace physics
```

- [ ] **Step 4: Rewrite `src/ecs/physics/SweptCollision.hpp`**

```cpp
#pragma once

#include <glm/vec3.hpp>
#include <span>

/// @brief Pure swept-collision math — no ECS types, no registry.
///
/// **Plane convention:** `dot(normal, p) > distance` is free space;
/// `dot(normal, p) < distance` is solid. The normal always points into free space.
///
/// Example planes (Y-up coordinate system):
/// - Floor at y=0:               `{ normal=(0,1,0),  distance=0    }`
/// - Ceiling at y=512:           `{ normal=(0,-1,0), distance=-512 }`
/// - Wall at x=256 (solid right): `{ normal=(-1,0,0), distance=-256 }`
namespace physics
{

/// @brief An infinite plane dividing free space from solid geometry.
struct Plane
{
    glm::vec3 normal; ///< Unit vector pointing into free (non-solid) space.
    float distance;   ///< Signed offset: `dot(normal, p) == distance` for points on the plane.
};

/// @brief Result of a swept AABB collision query.
struct HitResult
{
    bool hit{false};                     ///< True if the sweep intersected a plane.
    float tFirst{1.0f};                  ///< Fraction along the movement path [0..1] where the first hit occurs.
    glm::vec3 normal{0.0f, 1.0f, 0.0f}; ///< Surface normal at the contact point.
};

/// @brief Sweep an AABB along the path [start, end] against a list of infinite planes.
///
/// Uses the Minkowski-sum approach: each plane is expanded outward by the AABB
/// half-extents, reducing the problem to a ray-vs-expanded-plane intersection.
///
/// @param halfExtents  Half-dimensions of the AABB.
/// @param start        World-space start position (AABB centre).
/// @param end          World-space end position (AABB centre).
/// @param planes       World collision planes to test against.
/// @return             Earliest hit within the sweep, or `HitResult{hit=false}` if the path is clear.
/// @note               Entities that start already inside a plane are skipped.
///                     Depenetration is handled separately by CollisionSystem before calling this.
HitResult sweepAABB(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, std::span<const Plane> planes);

} // namespace physics
```

- [ ] **Step 5: Verify build**

```bash
cmake --build --preset debug
```

Expected: zero errors.

- [ ] **Step 6: Commit**

```bash
git add src/ecs/registry/Registry.hpp src/ecs/physics/
git commit -m "docs: convert physics and registry headers to Google Doxygen style"
```

---

## Task 4: Physics Implementations

**Files:**
- Modify: `src/ecs/physics/Movement.cpp`
- Modify: `src/ecs/physics/SweptCollision.cpp`

These files have no `// ----` section boxes — only inline algorithm comments, which stay as plain `//`. The only change is to make the existing comments slightly more precise where needed.

- [ ] **Step 1: Rewrite `src/ecs/physics/Movement.cpp`**

The logic is unchanged. Update only the comment style — remove any stray `//` lines that duplicate the header Doxygen, keep only algorithm-level commentary:

```cpp
#include "Movement.hpp"

#include "PhysicsConstants.hpp"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace physics
{

glm::vec3 applyGravity(glm::vec3 vel, float dt)
{
    vel.y -= k_gravity * dt;
    return vel;
}

glm::vec3 applyGroundFriction(glm::vec3 vel, float dt)
{
    // Only horizontal (XZ) velocity is affected by ground friction.
    const float k_speed = glm::length(glm::vec3(vel.x, 0.0f, vel.z));
    if (k_speed < 0.001f)
        return vel;

    // Quake trick: use k_stopSpeed as a minimum so friction is amplified at
    // low speeds, giving a crisp stop rather than an asymptotic glide.
    const float k_control  = std::max(k_speed, k_stopSpeed);
    const float k_drop     = k_control * k_friction * dt;
    const float k_newSpeed = std::max(0.0f, k_speed - k_drop) / k_speed;

    return {vel.x * k_newSpeed, vel.y, vel.z * k_newSpeed};
}

glm::vec3 accelerate(glm::vec3 vel, glm::vec3 wishDir, float wishSpeed, float accel, float dt)
{
    // Project current velocity onto the wish direction.
    // If we're already moving at or above wishSpeed in that direction, do nothing.
    const float k_currentSpeed = glm::dot(vel, wishDir);
    const float k_addSpeed     = wishSpeed - k_currentSpeed;
    if (k_addSpeed <= 0.0f)
        return vel;

    // Accelerate, but never overshoot wishSpeed in the wish direction.
    const float k_accelSpeed = std::min(accel * dt * wishSpeed, k_addSpeed);
    return vel + wishDir * k_accelSpeed;
}

glm::vec3 clipVelocity(glm::vec3 vel, glm::vec3 normal, float overbounce)
{
    const float k_backoff = glm::dot(vel, normal) * overbounce;
    return vel - normal * k_backoff;
}

glm::vec3 computeWishDir(float yaw, bool forward, bool back, bool left, bool right)
{
    // Build a local XZ move vector from key state.
    // +Z is the forward direction in world space at yaw=0.
    float moveX = 0.0f;
    float moveZ = 0.0f;
    if (forward) moveZ += 1.0f;
    if (back)    moveZ -= 1.0f;
    if (left)    moveX -= 1.0f;
    if (right)   moveX += 1.0f;

    if (moveX == 0.0f && moveZ == 0.0f)
        return glm::vec3{0.0f};

    // Rotate the local move vector by the player's yaw to get world-space direction.
    const float k_cosYaw = std::cos(yaw);
    const float k_sinYaw = std::sin(yaw);

    const glm::vec3 k_wish{
        moveX * k_cosYaw + moveZ * k_sinYaw,
        0.0f,
        -moveX * k_sinYaw + moveZ * k_cosYaw
    };

    return glm::normalize(k_wish);
}

} // namespace physics
```

- [ ] **Step 2: Rewrite `src/ecs/physics/SweptCollision.cpp`**

```cpp
#include "SweptCollision.hpp"

#include <glm/geometric.hpp>

namespace physics
{

HitResult sweepAABB(glm::vec3 halfExtents, glm::vec3 start, glm::vec3 end, std::span<const Plane> planes)
{
    HitResult result; // hit=false, tFirst=1.0 by default

    for (const Plane& plane : planes) {
        // Expand the plane outward by the AABB's extent in the plane's normal direction
        // (Minkowski sum). This lets us treat the sweep as a point vs. expanded plane.
        // r = how far the AABB "sticks out" in the normal direction.
        const float k_r = std::abs(plane.normal.x) * halfExtents.x
                        + std::abs(plane.normal.y) * halfExtents.y
                        + std::abs(plane.normal.z) * halfExtents.z;

        // Signed distances of the AABB centre from the (unexpanded) plane.
        const float k_distStart = glm::dot(plane.normal, start) - plane.distance;
        const float k_distEnd   = glm::dot(plane.normal, end)   - plane.distance;

        // Skip only if the entity is clearly inside the solid (not just touching).
        // Entities exactly AT the surface (k_distStart == k_r) must NOT be skipped —
        // they need a t=0 hit so grounded is set and velocity is clipped.
        if (k_distStart < k_r)
            continue;

        // Skip if not moving toward the plane (moving away or parallel).
        if (k_distEnd >= k_distStart)
            continue;

        // Time at which the front face of the AABB reaches the expanded plane.
        // Derivation: solve (k_distStart - k_r) + t*(k_distEnd - k_distStart) = 0
        const float k_t = (k_distStart - k_r) / (k_distStart - k_distEnd);

        if (k_t >= 0.0f && k_t < result.tFirst) {
            result.hit    = true;
            result.tFirst = k_t;
            result.normal = plane.normal;
        }
    }

    return result;
}

} // namespace physics
```

- [ ] **Step 3: Verify build**

```bash
cmake --build --preset debug
```

Expected: zero errors.

- [ ] **Step 4: Commit**

```bash
git add src/ecs/physics/Movement.cpp src/ecs/physics/SweptCollision.cpp
git commit -m "docs: clean up physics implementation comments"
```

---

## Task 5: ECS System Headers + Implementations

**Files:**
- Modify: `src/ecs/systems/Systems.hpp`
- Modify: `src/ecs/systems/MovementSystem.hpp`
- Modify: `src/ecs/systems/MovementSystem.cpp`
- Modify: `src/ecs/systems/CollisionSystem.hpp`
- Modify: `src/ecs/systems/CollisionSystem.cpp`

- [ ] **Step 1: Rewrite `src/ecs/systems/Systems.hpp`**

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief ECS systems namespace.
///
/// Each free function (or callable) represents one system. A system receives the
/// shared Registry and any per-frame state it needs, then queries and mutates components.
///
/// Concrete system implementations live in individual `.hpp`/`.cpp` pairs under this directory.
namespace systems
{

/// @brief Placeholder system — replace with real logic.
inline void update(Registry& /*registry*/, float /*dt*/) {}

} // namespace systems
```

- [ ] **Step 2: Rewrite `src/ecs/systems/MovementSystem.hpp`**

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Shared movement system — compiled identically on client and server.
///
/// Any divergence between client and server builds is a bug (breaks prediction).
///
/// **Reads:** InputSnapshot (optional), PlayerState, CollisionShape
/// **Writes:** Velocity, PlayerState (crouching/sliding), CollisionShape (crouch resize)
///
/// @note Position integration is NOT done here — CollisionSystem owns that via swept AABB.
namespace systems
{

/// @brief Apply one tick of player movement physics to all eligible entities.
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
void runMovement(Registry& registry, float dt);

} // namespace systems
```

- [ ] **Step 3: Rewrite `src/ecs/systems/MovementSystem.cpp`**

Replace the `// ---` section dividers inside the function with simpler plain `//` labels (no dashes). The `/// @brief` header goes only on the `static` helper constants anonymous namespace comment. No `// ----` boxes exist in this file, so it's mostly removing the long dash-lines from the two view section headers:

```cpp
#include "ecs/systems/MovementSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"

#include <glm/geometric.hpp>

namespace systems
{

namespace
{
constexpr float k_standingHalfHeight  = 36.0f;
constexpr float k_crouchingHalfHeight = 22.0f;

// How far the AABB centre moves when transitioning between standing and crouching.
// The feet stay at the same world position; only the centre moves.
constexpr float k_crouchCentreShift = k_standingHalfHeight - k_crouchingHalfHeight; // 14
} // namespace

void runMovement(Registry& registry, float dt)
{
    // Entities WITH InputSnapshot — full player movement.
    registry.view<Position, Velocity, PlayerState, CollisionShape, InputSnapshot>().each(
        [dt](Position& pos, Velocity& vel, PlayerState& state, CollisionShape& shape, const InputSnapshot& input) {
            // Crouch transition.
            // Position represents the AABB centre. When halfExtents.y changes, the centre
            // must shift so the feet stay at the same world position:
            //   feet = pos.y - halfExtents.y  (must remain constant)
            //
            // Stand → Crouch: halfExtents shrinks → lower centre by delta.
            // Crouch → Stand: halfExtents grows  → raise centre by delta.
            //
            // Without this adjustment, the entity ends up inside the floor after uncrouching,
            // causing the sweep to skip the plane and the entity to fall through.
            const bool k_wantsCrouch = input.crouch;
            if (k_wantsCrouch && !state.crouching) {
                // Transitioning to crouch: lower centre to keep feet in place.
                state.crouching      = true;
                shape.halfExtents.y  = k_crouchingHalfHeight;
                pos.value.y         -= k_crouchCentreShift;
            } else if (!k_wantsCrouch && state.crouching) {
                // Transitioning to stand: raise centre to keep feet in place.
                // CollisionSystem's depenetration pass will push the entity back
                // down if raising it puts the top through a ceiling.
                state.crouching      = false;
                shape.halfExtents.y  = k_standingHalfHeight;
                pos.value.y         += k_crouchCentreShift;
            }

            // Jump.
            if (input.jump && state.grounded && !state.crouching) {
                vel.value.y   = physics::k_jumpSpeed;
                state.grounded = false;
            }

            // Wish direction.
            const glm::vec3 k_wishDir =
                physics::computeWishDir(input.yaw, input.forward, input.back, input.left, input.right);

            // Ground movement.
            if (state.grounded) {
                vel.value = physics::applyGroundFriction(vel.value, dt);
                if (glm::length(k_wishDir) > 0.001f)
                    vel.value = physics::accelerate(
                        vel.value, k_wishDir, physics::k_maxGroundSpeed, physics::k_groundAccel, dt);
            }
            // Air movement.
            else {
                vel.value = physics::applyGravity(vel.value, dt);
                if (glm::length(k_wishDir) > 0.001f)
                    vel.value = physics::accelerate(
                        vel.value, k_wishDir, physics::k_airMaxSpeed, physics::k_airAccel, dt);
            }
        });

    // Entities WITHOUT InputSnapshot — physics only (gravity / friction).
    registry.view<Velocity, PlayerState>(entt::exclude<InputSnapshot>)
        .each([dt](Velocity& vel, const PlayerState& state) {
            if (state.grounded)
                vel.value = physics::applyGroundFriction(vel.value, dt);
            else
                vel.value = physics::applyGravity(vel.value, dt);
        });
}

} // namespace systems
```

- [ ] **Step 4: Rewrite `src/ecs/systems/CollisionSystem.hpp`**

```cpp
#pragma once

#include "ecs/physics/SweptCollision.hpp"
#include "ecs/registry/Registry.hpp"

#include <span>

/// @brief Shared collision system — compiled identically on client and server.
///
/// Any divergence between client and server builds is a bug (breaks prediction).
namespace systems
{

/// @brief Run one tick of swept-AABB collision for all physics entities.
///
/// For every entity with `[Position, Velocity, CollisionShape, PlayerState]`:
/// 1. Clears the `grounded` flag.
/// 2. Sweeps the AABB from current position toward `pos + vel * dt`.
/// 3. On hit: moves to the contact point, clips velocity, repeats up to 4 times
///    (Quake-style bumping handles corners and multi-surface contacts).
/// 4. Sets `grounded = true` if a floor surface (`normal.y > 0.7`) is hit.
///
/// @note Position integration lives here, **not** in MovementSystem.
///
/// @param registry  The ECS registry.
/// @param dt        Fixed physics delta time in seconds.
/// @param planes    World collision planes for this tick.
void runCollision(Registry& registry, float dt, std::span<const physics::Plane> planes);

} // namespace systems
```

- [ ] **Step 5: Rewrite `src/ecs/systems/CollisionSystem.cpp`**

Replace the `// ----` section boxes on private functions with `/// @brief`. Keep all phase comments and inline algorithm comments as plain `//`:

```cpp
#include "ecs/systems/CollisionSystem.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/physics/Movement.hpp"
#include "ecs/physics/PhysicsConstants.hpp"
#include "ecs/physics/SweptCollision.hpp"

#include <glm/geometric.hpp>

namespace systems
{

static constexpr float k_pushback            = 0.03125f;                         // Quake DIST_EPSILON
static constexpr float k_groundProbeDistance = physics::k_stepHeight;            // also used for slope snap

/// @brief Push the entity out of any planes it currently overlaps.
///
/// Runs before the bump loop. If the entity starts inside any plane (from shape
/// changes like uncrouch, spawn, or floating-point accumulation), push it out
/// along the plane normal until it is clearly outside.
///
/// Without this, sweepAABB's `if (distStart < k_r) continue` guard silently
/// skips penetrated planes, allowing the entity to fall through geometry.
static void depenetrate(glm::vec3& pos,
                        glm::vec3& vel,
                        const glm::vec3& halfExtents,
                        std::span<const physics::Plane> planes)
{
    for (const physics::Plane& plane : planes) {
        const float k_r    = std::abs(plane.normal.x) * halfExtents.x
                           + std::abs(plane.normal.y) * halfExtents.y
                           + std::abs(plane.normal.z) * halfExtents.z;
        const float k_dist = glm::dot(plane.normal, pos) - plane.distance;

        if (k_dist < k_r) {
            // Push entity out so it sits just outside the surface.
            const float k_overlap = k_r - k_dist;
            pos += plane.normal * (k_overlap + k_pushback);

            // Kill velocity into the plane so the entity doesn't immediately
            // re-penetrate on the next tick.
            const float k_into = glm::dot(vel, plane.normal);
            if (k_into < 0.0f)
                vel -= plane.normal * k_into;
        }
    }
}

/// @brief Attempt to step over a low obstacle when a wall is hit.
/// @return True if the step succeeded and position/velocity were updated.
static bool tryStepUp(glm::vec3& pos,
                      glm::vec3& vel,
                      const glm::vec3& halfExtents,
                      float remainingTime,
                      std::span<const physics::Plane> planes)
{
    const glm::vec3 k_stepVec{0.0f, physics::k_stepHeight, 0.0f};

    // 1. Lift straight up — abort if ceiling blocks.
    const glm::vec3       k_liftEnd   = pos + k_stepVec;
    const physics::HitResult k_lift   = physics::sweepAABB(halfExtents, pos, k_liftEnd, planes);
    if (k_lift.hit)
        return false;

    // 2. Sweep horizontally at step height — abort if still blocked.
    const glm::vec3       k_horizEnd  = k_liftEnd + glm::vec3{vel.x * remainingTime, 0.0f, vel.z * remainingTime};
    const physics::HitResult k_horiz  = physics::sweepAABB(halfExtents, k_liftEnd, k_horizEnd, planes);
    if (k_horiz.hit)
        return false;

    // 3. Drop back down — must land on a floor-like surface.
    const glm::vec3       k_dropEnd   = k_horizEnd - k_stepVec;
    const physics::HitResult k_drop   = physics::sweepAABB(halfExtents, k_horizEnd, k_dropEnd, planes);

    if (!k_drop.hit || k_drop.normal.y <= 0.7f)
        return false;

    pos    = k_horizEnd - k_stepVec * k_drop.tFirst;
    pos   += k_drop.normal * k_pushback;
    vel.y  = 0.0f;
    return true;
}

/// @brief Keep the entity glued to descending slopes and step-downs.
static void snapToGround(glm::vec3& pos,
                         glm::vec3& vel,
                         const glm::vec3& halfExtents,
                         std::span<const physics::Plane> planes)
{
    const glm::vec3         k_probeTarget = pos - glm::vec3{0.0f, k_groundProbeDistance, 0.0f};
    const physics::HitResult k_snap       = physics::sweepAABB(halfExtents, pos, k_probeTarget, planes);

    if (!k_snap.hit || k_snap.normal.y <= 0.7f)
        return;

    pos    = pos - glm::vec3{0.0f, k_groundProbeDistance * k_snap.tFirst, 0.0f};
    pos   += k_snap.normal * k_pushback;
    vel.y  = 0.0f;
}

void runCollision(Registry& registry, float dt, std::span<const physics::Plane> planes)
{
    registry.view<Position, Velocity, CollisionShape, PlayerState>().each(
        [dt, planes](Position& pos, Velocity& vel, const CollisionShape& shape, PlayerState& state) {
            const bool k_wasGrounded = state.grounded;
            state.grounded = false;

            // Phase 0 — Depenetration
            // Fix overlap introduced by shape changes (crouch/uncrouch), spawn, or
            // floating-point accumulation. Must run before the bump loop.
            depenetrate(pos.value, vel.value, shape.halfExtents, planes);

            // Phase 1 — Bump loop (collision response + stair stepping)
            float remainingTime = dt;

            for (int clip = 0; clip < 4 && remainingTime > 1e-5f; ++clip) {
                const glm::vec3       k_target = pos.value + vel.value * remainingTime;
                const physics::HitResult k_hit  = physics::sweepAABB(shape.halfExtents, pos.value, k_target, planes);

                if (!k_hit.hit) {
                    pos.value = k_target;
                    break;
                }

                pos.value    += vel.value * k_hit.tFirst * remainingTime;
                remainingTime *= (1.0f - k_hit.tFirst);

                const bool k_isFloor = k_hit.normal.y > 0.7f;

                if (k_isFloor) {
                    pos.value   += k_hit.normal * k_pushback;
                    vel.value    = physics::clipVelocity(vel.value, k_hit.normal, physics::k_overbounceFloor);
                    state.grounded = true;
                } else {
                    if (k_wasGrounded && tryStepUp(pos.value, vel.value, shape.halfExtents, remainingTime, planes)) {
                        state.grounded = true;
                        break;
                    }
                    pos.value += k_hit.normal * k_pushback;
                    vel.value  = physics::clipVelocity(vel.value, k_hit.normal, physics::k_overbounceWall);
                }
            }

            // Phase 2 — Slope sticking
            if (k_wasGrounded) {
                const float k_horizSpeed = glm::length(glm::vec3{vel.value.x, 0.0f, vel.value.z});
                if (k_horizSpeed > 0.001f)
                    snapToGround(pos.value, vel.value, shape.halfExtents, planes);
            }

            // Phase 3 — Ground probe
            const glm::vec3         k_probeTarget = pos.value - glm::vec3{0.0f, k_groundProbeDistance, 0.0f};
            const physics::HitResult k_probe       = physics::sweepAABB(shape.halfExtents, pos.value, k_probeTarget, planes);

            if (k_probe.hit && k_probe.normal.y > 0.7f)
                state.grounded = true;
        });
}

} // namespace systems
```

- [ ] **Step 6: Verify build**

```bash
cmake --build --preset debug
```

Expected: zero errors.

- [ ] **Step 7: Commit**

```bash
git add src/ecs/systems/
git commit -m "docs: convert ECS system headers and implementations to Google Doxygen style"
```

---

## Task 6: Client Class Headers

**Files:**
- Modify: `src/client/game/Game.hpp`
- Modify: `src/client/network/Client.hpp`
- Modify: `src/client/renderer/Renderer.hpp`
- Modify: `src/client/debug/DebugUI.hpp`

- [ ] **Step 1: Rewrite `src/client/game/Game.hpp`**

```cpp
#pragma once

#include "debug/DebugUI.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"
#include "renderer/Renderer.hpp"

#include <SDL3/SDL.h>

/// @brief Top-level client game object.
///
/// Owns all subsystems: window, ECS registry, renderer, debug UI, and network client.
/// Wired into SDL's application-callback API (SDL_AppInit / SDL_AppEvent / SDL_AppIterate / SDL_AppQuit).
class Game
{
public:
    /// @brief Initialise all subsystems and spawn the local player entity.
    /// @return False on any fatal initialisation error.
    bool init();

    /// @brief Forward an SDL event to ImGui and handle application-level keys.
    /// @param event  The SDL event to process.
    /// @return SDL_APP_SUCCESS to quit, SDL_APP_CONTINUE to keep running.
    SDL_AppResult event(SDL_Event* event);

    /// @brief Advance one frame: sample input, step physics, render.
    /// @return SDL_APP_CONTINUE normally; SDL_APP_SUCCESS on quit request.
    SDL_AppResult iterate();

    /// @brief Shut down all subsystems in reverse-init order.
    void quit();

private:
    /// @brief Physics tick rate. The renderer interpolates between ticks using the accumulator.
    static constexpr int   k_physicsHz = 128;
    static constexpr float k_physicsDt = 1.0f / static_cast<float>(k_physicsHz); ///< Seconds per physics tick.

    SDL_Window* window   = nullptr;
    DebugUI     debugUI;
    Renderer    renderer;
    Registry    registry;
    Client      client;

    Uint64 prevTime      = 0;     ///< SDL performance counter at the last iterate() call.
    float  accumulator   = 0.0f;  ///< Unprocessed physics time in seconds.
    int    tickCount     = 0;     ///< Total physics ticks elapsed since start.
    bool   mouseCaptured = true;  ///< True when relative mouse mode is active.
};
```

- [ ] **Step 2: Rewrite `src/client/network/Client.hpp`**

```cpp
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <SDL3_net/SDL_net.h>

/// @brief UDP datagram client — sends input to the server and receives state updates.
class Client
{
public:
    /// @brief Create the UDP socket and resolve the server address.
    /// @param addr  Hostname or IP address of the server.
    /// @param port  UDP port the server is listening on.
    /// @return False on socket creation or DNS failure.
    bool init(const char* addr, Uint16 port);

    /// @brief Close the socket and release the resolved address.
    void shutdown();

    /// @brief Send a raw datagram to the server.
    /// @param data  Pointer to the payload bytes.
    /// @param size  Payload length in bytes.
    /// @return False if the send fails.
    bool send(const void* data, int size);

    /// @brief Receive and process one pending datagram.
    /// @return True if a datagram was received, false if the queue is empty.
    bool poll();

private:
    NET_DatagramSocket* sock       = nullptr; ///< Bound UDP socket.
    NET_Address*        serverAddr = nullptr; ///< Resolved server address.
    Uint16              serverPort = 0;       ///< Server UDP port.
};
```

- [ ] **Step 3: Rewrite `src/client/renderer/Renderer.hpp`**

```cpp
#pragma once

#include <SDL3/SDL.h>

/// @brief SDL3 GPU pipeline (Vulkan · Metal · DX12).
///
/// Also owns the `imgui_impl_sdlgpu3` render backend. The ImGui context and
/// SDL3 input backend are owned by DebugUI — initialise DebugUI first, shut it down last.
///
/// Shaders: `shaders/triangle.vert` + `shaders/triangle.frag`
/// (compiled GLSL → SPIR-V at build time via glslc/glslangValidator).
class Renderer
{
public:
    /// @brief Initialise the GPU device, pipeline, and ImGui GPU backend.
    /// @param window  The SDL window to render into.
    /// @return False on any fatal GPU error.
    /// @pre An ImGui context must already exist (created by DebugUI::init).
    bool init(SDL_Window* window);

    /// @brief Submit the scene geometry and ImGui draw data for one frame.
    void drawFrame();

    /// @brief Release all GPU resources. Waits for GPU idle before freeing.
    /// @pre Call before the SDL window is destroyed.
    void quit();

private:
    SDL_Window*             window   = nullptr; ///< The SDL window being rendered into.
    SDL_GPUDevice*          device   = nullptr; ///< The SDL GPU device.
    SDL_GPUGraphicsPipeline* pipeline = nullptr; ///< The scene graphics pipeline.
};
```

- [ ] **Step 4: Rewrite `src/client/debug/DebugUI.hpp`**

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

/// @brief Live ECS inspector overlay powered by Dear ImGui.
///
/// **Ownership split:**
/// - DebugUI  — ImGui context, `imgui_impl_sdl3` input backend, UI state
/// - Renderer — `imgui_impl_sdlgpu3` render backend (owns the GPU device)
///
/// **Initialisation order in Game::init():**
/// 1. `debugUI.init(window)` — context must exist before GPU backend init
/// 2. `renderer.init(window)` — GPU backend init happens here
///
/// **Shutdown order in Game::quit():**
/// 1. `renderer.quit()` — GPU backend shutdown first
/// 2. `debugUI.shutdown()` — context destroyed last
class DebugUI
{
public:
    /// @brief Create the ImGui context and initialise the SDL3 input backend.
    /// @param window  The SDL window receiving input events.
    /// @return False if ImGui backend initialisation fails.
    bool init(SDL_Window* window);

    /// @brief Destroy the ImGui context and shut down the SDL3 input backend.
    void shutdown();

    /// @brief Forward an SDL event to the ImGui input backend.
    /// @param event  The event to process.
    void processEvent(const SDL_Event* event);

    /// @brief Begin a new ImGui frame. Call before any ImGui draw calls.
    void newFrame();

    /// @brief Build the ECS inspector window contents.
    /// @param registry   The ECS registry to inspect.
    /// @param tickCount  Total physics ticks elapsed (displayed in the stats bar).
    void buildUI(const Registry& registry, int tickCount);

    /// @brief Finalise the ImGui frame. Call after all ImGui draw calls, before Renderer::drawFrame().
    void render();

private:
    // Per-component visibility toggles — persistent across frames.
    bool showPosition      = true;
    bool showPrevPosition  = false;
    bool showVelocity      = true;
    bool showCollisionShape = true;
    bool showPlayerState   = true;
    bool showInputSnapshot = true;
    bool showViewAngles    = true; ///< Yaw/pitch displayed in degrees for readability.
};
```

- [ ] **Step 5: Verify build**

```bash
cmake --build --preset debug
```

Expected: zero errors.

- [ ] **Step 6: Commit**

```bash
git add src/client/game/Game.hpp src/client/network/Client.hpp \
        src/client/renderer/Renderer.hpp src/client/debug/DebugUI.hpp
git commit -m "docs: convert client class headers to Google Doxygen style"
```

---

## Task 7: Client Implementations

**Files:**
- Modify: `src/client/game/Game.cpp`
- Modify: `src/client/renderer/Renderer.cpp`
- Modify: `src/client/debug/DebugUI.cpp`
- Modify: `src/client/network/Client.cpp`

The logic in each file is unchanged. Changes:
- Replace `// --- Section ---` dash-line dividers inside `iterate()` with simple `//` labels
- Replace `// ----` boxes on private helpers in `Renderer.cpp` with `/// @brief`
- Replace `// ----` section labels in `DebugUI.cpp` `buildUI()` with plain `//`

- [ ] **Step 1: Rewrite `src/client/game/Game.cpp`**

```cpp
#include "Game.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"
#include "systems/InputSampleSystem.hpp"

#include <SDL3/SDL_video.h>
#include <SDL3_net/SDL_net.h>
#include <algorithm>

// World geometry for the current test scene: a single floor plane at y=0.
// Will be replaced by a proper World object when map loading is implemented.
static const std::array k_worldPlanes{
    physics::Plane{.normal = glm::vec3{0.0f, 1.0f, 0.0f}, .distance = 0.0f}
};

bool Game::init()
{
    SDL_SetAppMetadata("group2", "0.1.0", "com.cse125.group2");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if (!NET_Init()) {
        SDL_Log("NET_Init() failed: %s", SDL_GetError());
        return false;
    }

    window = SDL_CreateWindow("group2", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    // DebugUI must be initialised before Renderer — it creates the ImGui
    // context that the GPU render backend (in Renderer::init) requires.
    if (!debugUI.init(window)) {
        SDL_Log("DebugUI init failed");
        SDL_DestroyWindow(window);
        return false;
    }

    if (!renderer.init(window)) {
        SDL_Log("Renderer init failed");
        debugUI.shutdown();
        SDL_DestroyWindow(window);
        return false;
    }

    if (!client.init("127.0.0.1", 9999)) {
        SDL_Log("Failed to connect to server");
        renderer.quit();
        debugUI.shutdown();
        SDL_DestroyWindow(window);
        return false;
    }

    // Grab the mouse into relative mode so camera look works immediately.
    SDL_SetWindowRelativeMouseMode(window, true);
    mouseCaptured = true;

    // Spawn the local player entity with all physics and input components.
    const glm::vec3     k_startPos{0.0f, 200.0f, 0.0f};
    const entt::entity  k_player = registry.create();
    registry.emplace<Position>(k_player, k_startPos);
    registry.emplace<PreviousPosition>(k_player, k_startPos);
    registry.emplace<Velocity>(k_player);
    registry.emplace<CollisionShape>(k_player);
    registry.emplace<PlayerState>(k_player);
    registry.emplace<InputSnapshot>(k_player);
    registry.emplace<LocalPlayer>(k_player);

    prevTime = SDL_GetPerformanceCounter();
    SDL_Log("[client] local player spawned at (0, 200, 0), physicsHz=%d", k_physicsHz);
    return true;
}

SDL_AppResult Game::event(SDL_Event* event)
{
    // Forward every event to ImGui first so it can capture keyboard/mouse
    // when the cursor is hovering over a window.
    debugUI.processEvent(event);

    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;

    if (event->type == SDL_EVENT_KEY_DOWN) {
        switch (event->key.key) {
        case SDLK_Q:
            return SDL_APP_SUCCESS;

        // ESC — toggle mouse capture so the player can reach the ImGui window.
        case SDLK_ESCAPE:
            mouseCaptured = !mouseCaptured;
            SDL_SetWindowRelativeMouseMode(window, mouseCaptured);
            break;

        // F1 — send a test hello packet to the server.
        case SDLK_F1: {
            static constexpr char k_helloMsg[] = "Hello from client!";
            client.send(k_helloMsg, static_cast<int>(sizeof(k_helloMsg) - 1));
            SDL_Log("Sent test packet to server");
            break;
        }

        default:
            break;
        }
    }

    // Re-capture mouse on window click while uncaptured (standard FPS behaviour).
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && !mouseCaptured) {
        mouseCaptured = true;
        SDL_SetWindowRelativeMouseMode(window, true);
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult Game::iterate()
{
    // ImGui frame start — must happen before any ImGui calls this frame.
    debugUI.newFrame();

    // Sample input once per frame before the physics loop.
    // Mouse deltas are consumed here; running inside the loop would
    // accumulate them multiple times per frame at high frame rates.
    if (mouseCaptured)
        systems::runInputSample(registry);

    // Compute frame time.
    const Uint64 k_perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 k_now      = SDL_GetPerformanceCounter();

    float frameTime = static_cast<float>(k_now - prevTime) / static_cast<float>(k_perfFreq);
    prevTime        = k_now;
    frameTime       = std::min(frameTime, 0.25f); // clamp to avoid spiral-of-death
    accumulator    += frameTime;

    // Fixed-step physics loop.
    while (accumulator >= k_physicsDt) {
        registry.view<Position, PreviousPosition>().each(
            [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

        systems::runMovement(registry, k_physicsDt);
        systems::runCollision(registry, k_physicsDt, k_worldPlanes);

        accumulator -= k_physicsDt;
        ++tickCount;
    }

    // Network receive.
    while (client.poll()) {}

    // Build debug UI and render.
    debugUI.buildUI(registry, tickCount);
    debugUI.render();
    renderer.drawFrame();

    return SDL_APP_CONTINUE;
}

void Game::quit()
{
    renderer.quit();
    debugUI.shutdown();
    client.shutdown();
    SDL_DestroyWindow(window);
    NET_Quit();
    SDL_Quit();
}
```

- [ ] **Step 2: Rewrite `src/client/renderer/Renderer.cpp`**

Replace the `// ---- Internal helpers ----` box and `// ---- Renderer implementation ----` box. The `loadShader` helper gets `/// @brief`:

```cpp
#include "Renderer.hpp"

#include <backends/imgui_impl_sdlgpu3.h>
#include <imgui.h>

namespace
{

/// @brief Load a compiled shader from disk and create an SDL GPU shader object.
/// @param dev                 The GPU device.
/// @param path                Path to the compiled shader file (.spv or .msl).
/// @param format              Shader format (SPIR-V or MSL).
/// @param stage               Vertex or fragment stage.
/// @param samplerCount        Number of texture samplers declared in the shader.
/// @param uniformBufferCount  Number of uniform buffers declared in the shader.
/// @param storageBufferCount  Number of storage buffers declared in the shader.
/// @param storageTextureCount Number of storage textures declared in the shader.
/// @return The created shader, or nullptr on failure (error logged via SDL_Log).
SDL_GPUShader* loadShader(SDL_GPUDevice*    dev,
                          const char*       path,
                          SDL_GPUShaderFormat format,
                          SDL_GPUShaderStage  stage,
                          Uint32 samplerCount,
                          Uint32 uniformBufferCount,
                          Uint32 storageBufferCount,
                          Uint32 storageTextureCount)
{
    size_t codeSize = 0;
    void*  code     = SDL_LoadFile(path, &codeSize);
    if (!code) {
        SDL_Log("Renderer: failed to load shader %s: %s", path, SDL_GetError());
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code               = static_cast<const Uint8*>(code);
    info.code_size          = static_cast<Uint32>(codeSize);
    info.format             = format;
    info.stage              = stage;
    info.num_samplers       = samplerCount;
    info.num_uniform_buffers = uniformBufferCount;
    info.num_storage_buffers = storageBufferCount;
    info.num_storage_textures = storageTextureCount;

    // SPIR-V entry point is "main"; spirv-cross renames it to "main0" in MSL
    // (Metal forbids a function literally named "main").
    info.entrypoint = (format == SDL_GPU_SHADERFORMAT_MSL) ? "main0" : "main";

    SDL_GPUShader* shader = SDL_CreateGPUShader(dev, &info);
    SDL_free(code);

    if (!shader)
        SDL_Log("Renderer: SDL_CreateGPUShader(%s) failed: %s", path, SDL_GetError());
    return shader;
}

} // namespace

bool Renderer::init(SDL_Window* win)
{
    window = win;

    constexpr SDL_GPUShaderFormat k_wantedFormats = SDL_GPU_SHADERFORMAT_SPIRV
#ifdef HAVE_MSL_SHADERS
                                                  | SDL_GPU_SHADERFORMAT_MSL
#endif
        ;

    device = SDL_CreateGPUDevice(k_wantedFormats, /*debug_mode=*/false, nullptr);
    if (!device) {
        SDL_Log("Renderer: SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return false;
    }
    SDL_Log("Renderer: GPU driver = %s", SDL_GetGPUDeviceDriver(device));

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("Renderer: SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return false;
    }

    const SDL_GPUShaderFormat k_available  = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderFormat       activeFormat = SDL_GPU_SHADERFORMAT_INVALID;

    if (k_available & SDL_GPU_SHADERFORMAT_SPIRV)
        activeFormat = SDL_GPU_SHADERFORMAT_SPIRV;
#ifdef HAVE_MSL_SHADERS
    else if (k_available & SDL_GPU_SHADERFORMAT_MSL)
        activeFormat = SDL_GPU_SHADERFORMAT_MSL;
#endif

    if (activeFormat == SDL_GPU_SHADERFORMAT_INVALID) {
        SDL_Log("Renderer: no supported shader format (got 0x%x)", static_cast<unsigned>(k_available));
        return false;
    }

    // ImGui GPU backend setup.
    // The ImGui context and SDL3 input backend were already initialised by
    // DebugUI::init(). We just hook up the GPU render backend here.
    const SDL_GPUTextureFormat k_colorFmt = SDL_GetGPUSwapchainTextureFormat(device, window);

    ImGui_ImplSDLGPU3_InitInfo imguiInfo{};
    imguiInfo.Device            = device;
    imguiInfo.ColorTargetFormat = k_colorFmt;
    imguiInfo.MSAASamples       = SDL_GPU_SAMPLECOUNT_1;

    if (!ImGui_ImplSDLGPU3_Init(&imguiInfo)) {
        SDL_Log("Renderer: ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    // Scene pipeline (triangle).
    const char* const k_base = SDL_GetBasePath();
    const char* const k_ext  = (activeFormat == SDL_GPU_SHADERFORMAT_MSL) ? ".msl" : ".spv";

    char vertPath[512], fragPath[512];
    SDL_snprintf(vertPath, sizeof(vertPath), "%sshaders/triangle.vert%s", k_base ? k_base : "", k_ext);
    SDL_snprintf(fragPath, sizeof(fragPath), "%sshaders/triangle.frag%s", k_base ? k_base : "", k_ext);

    SDL_GPUShader* vert = loadShader(device, vertPath, activeFormat, SDL_GPU_SHADERSTAGE_VERTEX,   0, 0, 0, 0);
    SDL_GPUShader* frag = loadShader(device, fragPath, activeFormat, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, 0, 0, 0);
    if (!vert || !frag) {
        SDL_ReleaseGPUShader(device, vert);
        SDL_ReleaseGPUShader(device, frag);
        return false;
    }

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = k_colorFmt;

    SDL_GPUGraphicsPipelineCreateInfo pci{};
    pci.vertex_shader                             = vert;
    pci.fragment_shader                           = frag;
    pci.primitive_type                            = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pci.target_info.color_target_descriptions     = &colorTarget;
    pci.target_info.num_color_targets             = 1;
    pci.rasterizer_state.fill_mode                = SDL_GPU_FILLMODE_FILL;
    pci.rasterizer_state.cull_mode                = SDL_GPU_CULLMODE_NONE;

    pipeline = SDL_CreateGPUGraphicsPipeline(device, &pci);

    SDL_ReleaseGPUShader(device, vert);
    SDL_ReleaseGPUShader(device, frag);

    if (!pipeline) {
        SDL_Log("Renderer: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void Renderer::drawFrame()
{
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
    if (!cmd)
        return;

    SDL_GPUTexture* swapchain = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_AcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h) || !swapchain) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    // Upload ImGui vertex/index buffers via an internal copy pass.
    // This must happen BEFORE the render pass begins.
    ImDrawData* const k_drawData = ImGui::GetDrawData();
    if (k_drawData)
        ImGui_ImplSDLGPU3_PrepareDrawData(k_drawData, cmd);

    SDL_GPUColorTargetInfo ct{};
    ct.texture     = swapchain;
    ct.clear_color = {.r = 0.10f, .g = 0.10f, .b = 0.10f, .a = 1.0f};
    ct.load_op     = SDL_GPU_LOADOP_CLEAR;
    ct.store_op    = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &ct, 1, nullptr);

    // Scene geometry.
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);

    // ImGui overlay — drawn last so it sits on top of scene geometry.
    if (k_drawData)
        ImGui_ImplSDLGPU3_RenderDrawData(k_drawData, cmd, pass);

    SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
}

void Renderer::quit()
{
    if (device) {
        SDL_WaitForGPUIdle(device);
        ImGui_ImplSDLGPU3_Shutdown();
        if (pipeline)
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
    }
    pipeline = nullptr;
    device   = nullptr;
    window   = nullptr;
}
```

- [ ] **Step 3: Rewrite `src/client/debug/DebugUI.cpp`**

Replace the `// ---- Section ----` comment dividers inside `buildUI()` with plain `//` section labels:

```cpp
#include "debug/DebugUI.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlgpu3.h>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

bool DebugUI::init(SDL_Window* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLGPU(window)) {
        SDL_Log("DebugUI: ImGui_ImplSDL3_InitForSDLGPU failed");
        return false;
    }

    return true;
}

void DebugUI::shutdown()
{
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void DebugUI::processEvent(const SDL_Event* event)
{
    ImGui_ImplSDL3_ProcessEvent(event);
}

void DebugUI::newFrame()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void DebugUI::buildUI(const Registry& registry, const int tickCount)
{
    ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({480.0f, 580.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("ECS Inspector");

    // Key bindings reminder
    ImGui::TextDisabled("ESC: toggle mouse  |  Q: quit  |  F1: test packet");
    ImGui::Separator();

    // Component visibility toggles
    ImGui::SeparatorText("Components");
    ImGui::Checkbox("Position",       &showPosition);
    ImGui::SameLine();
    ImGui::Checkbox("PrevPosition",   &showPrevPosition);
    ImGui::Checkbox("Velocity",       &showVelocity);
    ImGui::SameLine();
    ImGui::Checkbox("CollisionShape", &showCollisionShape);
    ImGui::Checkbox("PlayerState",    &showPlayerState);
    ImGui::SameLine();
    ImGui::Checkbox("InputSnapshot",  &showInputSnapshot);
    ImGui::Checkbox("View Angles",    &showViewAngles);

    // Stats bar
    ImGui::Separator();
    ImGui::Text("Physics tick: %d", tickCount);

    const auto* const k_entityStorage = registry.storage<entt::entity>();

    int entityCount = 0;
    if (k_entityStorage)
        for (const auto entity : *k_entityStorage)
            if (registry.valid(entity))
                ++entityCount;

    ImGui::SameLine(0.0f, 20.0f);
    ImGui::Text("Entities: %d", entityCount);
    ImGui::Separator();

    if (!k_entityStorage) {
        ImGui::End();
        return;
    }

    // Per-entity sections
    for (const entt::entity entity : *k_entityStorage) {
        if (!registry.valid(entity))
            continue;

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));

        // Build label — append [LOCAL PLAYER] tag for the locally controlled entity.
        char       entityLabel[48];
        const bool k_isLocal = registry.all_of<LocalPlayer>(entity);
        SDL_snprintf(entityLabel,
                     sizeof(entityLabel),
                     k_isLocal ? "Entity #%u  [LOCAL PLAYER]" : "Entity #%u",
                     static_cast<unsigned>(entt::to_integral(entity)));

        if (ImGui::CollapsingHeader(entityLabel, ImGuiTreeNodeFlags_DefaultOpen)) {
            // Vec3 component table
            constexpr ImGuiTableFlags k_tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;

            if (ImGui::BeginTable("##vec3", 4, k_tableFlags)) {
                ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("X",         ImGuiTableColumnFlags_WidthFixed,  82.0f);
                ImGui::TableSetupColumn("Y",         ImGuiTableColumnFlags_WidthFixed,  82.0f);
                ImGui::TableSetupColumn("Z",         ImGuiTableColumnFlags_WidthFixed,  82.0f);
                ImGui::TableHeadersRow();

                const auto vec3Row = [](const char* name, const glm::vec3& v) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
                    ImGui::TableSetColumnIndex(1); ImGui::Text("%8.3f", static_cast<double>(v.x));
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%8.3f", static_cast<double>(v.y));
                    ImGui::TableSetColumnIndex(3); ImGui::Text("%8.3f", static_cast<double>(v.z));
                };

                if (showPosition      && registry.all_of<Position>(entity))
                    vec3Row("Position",          registry.get<Position>(entity).value);
                if (showPrevPosition  && registry.all_of<PreviousPosition>(entity))
                    vec3Row("PrevPosition",      registry.get<PreviousPosition>(entity).value);
                if (showVelocity      && registry.all_of<Velocity>(entity))
                    vec3Row("Velocity",          registry.get<Velocity>(entity).value);
                if (showCollisionShape && registry.all_of<CollisionShape>(entity))
                    vec3Row("CollisionShape he", registry.get<CollisionShape>(entity).halfExtents);

                ImGui::EndTable();
            }

            // View angles (degrees — easier to read than radians)
            if (showViewAngles && registry.all_of<InputSnapshot>(entity)) {
                const auto& c = registry.get<InputSnapshot>(entity);
                ImGui::Text("View Angles   yaw: %7.2f°   pitch: %7.2f°   roll: %6.2f°",
                            static_cast<double>(glm::degrees(c.yaw)),
                            static_cast<double>(glm::degrees(c.pitch)),
                            static_cast<double>(glm::degrees(c.roll)));
            }

            // PlayerState
            if (showPlayerState && registry.all_of<PlayerState>(entity)) {
                const auto& c = registry.get<PlayerState>(entity);
                ImGui::Text("PlayerState   grounded:%-3s  crouching:%-3s  sliding:%-3s",
                            c.grounded  ? "YES" : "NO",
                            c.crouching ? "YES" : "NO",
                            c.sliding   ? "YES" : "NO");
            }

            // InputSnapshot
            if (showInputSnapshot && registry.all_of<InputSnapshot>(entity)) {
                const auto& c = registry.get<InputSnapshot>(entity);
                ImGui::Text("InputSnapshot  tick: %u", c.tick);
                ImGui::Text("  fwd:%-3s  back:%-3s  left:%-3s  right:%-3s  jump:%-3s  crouch:%-3s",
                            c.forward ? "Y" : "N",
                            c.back    ? "Y" : "N",
                            c.left    ? "Y" : "N",
                            c.right   ? "Y" : "N",
                            c.jump    ? "Y" : "N",
                            c.crouch  ? "Y" : "N");
            }
        }

        ImGui::PopID();
    }

    ImGui::End();
}

void DebugUI::render()
{
    ImGui::Render();
}
```

- [ ] **Step 4: `src/client/network/Client.cpp` — no changes needed**

This file has no `// ----` section boxes and no doc comments that need updating. Verify it still compiles as-is.

- [ ] **Step 5: Verify build**

```bash
cmake --build --preset debug
```

Expected: zero errors.

- [ ] **Step 6: Commit**

```bash
git add src/client/game/Game.cpp src/client/renderer/Renderer.cpp \
        src/client/debug/DebugUI.cpp
git commit -m "docs: clean up client implementation comments"
```

---

## Task 8: Client System Headers

**Files:**
- Modify: `src/client/systems/InputSampleSystem.hpp`
- Modify: `src/client/systems/PredictionSystem.hpp`
- Modify: `src/client/systems/InputSendSystem.hpp`
- Modify: `src/client/systems/ReconciliationSystem.hpp`

- [ ] **Step 1: Rewrite `src/client/systems/InputSampleSystem.hpp`**

```cpp
#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <glm/trigonometric.hpp>

/// @brief Client-only input sampling system.
///
/// Reads SDL keyboard and mouse state each frame and writes the result into
/// the InputSnapshot component of the LocalPlayer entity.
///
/// @note Must be called **once per frame**, before the physics accumulator loop,
///       so the snapshot is fresh for every physics tick that fires that frame.
namespace systems
{

/// @brief Sample keyboard and mouse state into the local player's InputSnapshot.
/// @param registry          The ECS registry.
/// @param mouseSensitivity  Mouse sensitivity in radians per pixel (default 0.002).
inline void runInputSample(Registry& registry, float mouseSensitivity = 0.002f)
{
    const bool* const k_keys = SDL_GetKeyboardState(nullptr);

    float mdx = 0.0f;
    float mdy = 0.0f;
    SDL_GetRelativeMouseState(&mdx, &mdy);

    registry.view<InputSnapshot, LocalPlayer>().each([&](InputSnapshot& snap) {
        snap.forward = k_keys[SDL_SCANCODE_W];
        snap.back    = k_keys[SDL_SCANCODE_S];
        snap.left    = k_keys[SDL_SCANCODE_A];
        snap.right   = k_keys[SDL_SCANCODE_D];
        snap.jump    = k_keys[SDL_SCANCODE_SPACE];
        snap.crouch  = k_keys[SDL_SCANCODE_LCTRL];

        // Accumulate mouse deltas into absolute yaw.
        snap.yaw += mdx * mouseSensitivity;

        // Keep yaw in [-π, π] to avoid float precision drift over time.
        snap.yaw = std::fmod(snap.yaw, glm::radians(360.0f));

        // Clamp pitch to avoid gimbal-lock at the poles.
        snap.pitch = std::clamp(snap.pitch + mdy * mouseSensitivity,
                                -glm::radians(89.0f),
                                 glm::radians(89.0f));
    });
}

} // namespace systems
```

- [ ] **Step 2: Rewrite `src/client/systems/PredictionSystem.hpp`**

Replace the IDE-generated stub (pragma guards + auto-date comment) with a clean `#pragma once` stub:

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Client-side prediction system (not yet implemented).
///
/// Will apply local input immediately without waiting for server confirmation,
/// storing snapshots in a ring buffer for reconciliation.
namespace systems
{

// TODO: implement runPrediction()

} // namespace systems
```

- [ ] **Step 3: Rewrite `src/client/systems/InputSendSystem.hpp`**

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Serialises and sends the local player's InputSnapshot to the server (not yet implemented).
namespace systems
{

// TODO: implement runInputSend()

} // namespace systems
```

- [ ] **Step 4: Rewrite `src/client/systems/ReconciliationSystem.hpp`**

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Reconciles server state against the client's predicted history (not yet implemented).
///
/// When the server sends a correction, this system rewinds the ring buffer to the
/// authoritative tick and re-simulates forward to the current tick.
namespace systems
{

// TODO: implement runReconciliation()

} // namespace systems
```

- [ ] **Step 5: Verify build**

```bash
cmake --build --preset debug
```

Expected: zero errors.

- [ ] **Step 6: Commit**

```bash
git add src/client/systems/
git commit -m "docs: convert client system headers to Google Doxygen style"
```

---

## Task 9: Server Headers + Implementations

**Files:**
- Modify: `src/server/game/ServerGame.hpp`
- Modify: `src/server/game/ServerGame.cpp`
- Modify: `src/server/network/Server.hpp`
- Modify: `src/server/network/Server.cpp`
- Modify: `src/server/systems/BroadcastSystem.hpp`
- Modify: `src/server/systems/InputReceiveSystem.hpp`

- [ ] **Step 1: Rewrite `src/server/game/ServerGame.hpp`**

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"
#include "network/Server.hpp"

#include <SDL3/SDL.h>

/// @brief Top-level server game loop.
///
/// Owns the ECS registry and the network Server. Each tick it drains
/// incoming datagrams, runs all ECS systems, and broadcasts state.
class ServerGame
{
public:
    /// @brief Bind to the given address and port, spawn test entities.
    /// @param addr       Hostname or IP to bind to (e.g. "127.0.0.1").
    /// @param port       UDP port to listen on.
    /// @param tickRateHz Physics tick rate in Hz (default 128).
    /// @return False on network or initialisation failure.
    bool init(const char* addr, Uint16 port, int tickRateHz = 128);

    /// @brief Block and run the game loop until shutdown() is called.
    void run();

    /// @brief Signal the loop to stop and release all resources.
    void shutdown();

private:
    /// @brief Advance one physics tick.
    /// @param dt Fixed delta time in seconds (1 / tickRateHz).
    void tick(float dt);

    Server   server;
    Registry registry;
    bool     running    = false; ///< Loop continues while true.
    int      tickRateHz = 128;   ///< Physics ticks per second.
    int      tickCount  = 0;     ///< Total ticks since start, used for periodic logging.
};
```

- [ ] **Step 2: Rewrite `src/server/game/ServerGame.cpp`**

```cpp
#include "ServerGame.hpp"

#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/PlayerState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/systems/CollisionSystem.hpp"
#include "ecs/systems/MovementSystem.hpp"

#include <SDL3/SDL.h>

// World geometry for the current test scene: a single floor plane at y=0.
// The normal (0,1,0) points upward into free space; distance=0 places it at the origin.
// Will be replaced by a proper World object when map loading is implemented.
static const std::array k_worldPlanes{
    physics::Plane{.normal = glm::vec3{0.0f, 1.0f, 0.0f}, .distance = 0.0f}
};

bool ServerGame::init(const char* addr, Uint16 port, int hz)
{
    tickRateHz = hz;

    if (!server.init(addr, port))
        return false;

    // Spawn a test entity: starts at y=200, not grounded — will fall and land.
    const entt::entity k_testEntity = registry.create();
    registry.emplace<Position>(k_testEntity, glm::vec3{0.0f, 200.0f, 0.0f});
    registry.emplace<Velocity>(k_testEntity);
    registry.emplace<CollisionShape>(k_testEntity); // default: 32×72×32 standing AABB
    registry.emplace<PlayerState>(k_testEntity);

    SDL_Log("[server] spawned test entity at (0, 200, 0), tickRateHz=%d", tickRateHz);
    return true;
}

void ServerGame::run()
{
    running = true;

    const float  k_dt           = 1.0f / static_cast<float>(tickRateHz);
    const Uint64 k_perfFreq     = SDL_GetPerformanceFrequency();
    const Uint64 k_tickDuration = k_perfFreq / static_cast<Uint64>(tickRateHz);
    Uint64       nextTick       = SDL_GetPerformanceCounter();

    while (running) {
        server.poll();
        tick(k_dt);

        nextTick += k_tickDuration;
        const Uint64 k_now = SDL_GetPerformanceCounter();
        if (k_now < nextTick) {
            const Sint64 k_sleepMs = static_cast<Sint64>((nextTick - k_now) * 1000 / k_perfFreq) - 1;
            if (k_sleepMs > 0)
                SDL_Delay(static_cast<Uint32>(k_sleepMs));

            // Spin-wait for the remaining sub-millisecond.
            while (SDL_GetPerformanceCounter() < nextTick) {}
        }
    }
}

void ServerGame::shutdown()
{
    running = false;
    server.shutdown();
}

void ServerGame::tick(float dt)
{
    systems::runMovement(registry, dt);
    systems::runCollision(registry, dt, k_worldPlanes);

    // Log once per second so we can watch the test entity fall and land.
    ++tickCount;
    if (tickCount % tickRateHz == 0) {
        registry.view<Position>().each([this](const Position& pos) {
            SDL_Log("[server] tick %d | pos (%.1f, %.1f, %.1f)",
                    tickCount,
                    static_cast<double>(pos.value.x),
                    static_cast<double>(pos.value.y),
                    static_cast<double>(pos.value.z));
        });
    }
}
```

- [ ] **Step 3: Rewrite `src/server/network/Server.hpp`**

```cpp
#pragma once

#include <SDL3/SDL_stdinc.h>
#include <SDL3_net/SDL_net.h>

/// @brief UDP datagram socket — receives client packets and echoes them back.
///
/// Call poll() every tick to drain incoming datagrams.
/// Extend handleDatagram() with proper packet dispatch as the game protocol grows.
class Server
{
public:
    /// @brief Bind a UDP socket to the given address and port.
    /// @param addr  Hostname or IP to bind to (e.g. "127.0.0.1").
    /// @param port  UDP port to listen on.
    /// @return False on DNS or socket creation failure.
    bool init(const char* addr, Uint16 port);

    /// @brief Close the socket and release resources.
    void shutdown();

    /// @brief Drain all pending datagrams for this tick.
    void poll();

private:
    /// @brief Process a single received datagram (currently echo-back only).
    /// @param dgram  The received datagram (caller retains ownership).
    void handleDatagram(NET_Datagram* dgram);

    NET_DatagramSocket* sock = nullptr; ///< Bound UDP socket.
};
```

- [ ] **Step 4: `src/server/network/Server.cpp` — no changes needed**

This file has no `// ----` boxes and the only comment is `// Echo back to sender.` which is fine as-is.

- [ ] **Step 5: Rewrite `src/server/systems/BroadcastSystem.hpp`**

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Serialises ECS state and broadcasts it to all connected clients (not yet implemented).
namespace systems
{

// TODO: implement runBroadcast()

} // namespace systems
```

- [ ] **Step 6: Rewrite `src/server/systems/InputReceiveSystem.hpp`**

```cpp
#pragma once

#include "ecs/registry/Registry.hpp"

/// @brief Deserialises incoming client InputSnapshot packets and writes them into the registry (not yet implemented).
namespace systems
{

// TODO: implement runInputReceive()

} // namespace systems
```

- [ ] **Step 7: Verify build**

```bash
cmake --build --preset debug
```

Expected: zero errors.

- [ ] **Step 8: Commit**

```bash
git add src/server/
git commit -m "docs: convert server headers and implementations to Google Doxygen style"
```

---

## Task 10: Generate and Verify Docs

- [ ] **Step 1: Generate HTML documentation**

```bash
cmake --build --preset debug --target docs
```

Expected: Doxygen runs, outputs to `build/docs/html/`. Exit code 0.

- [ ] **Step 2: Spot-check the output**

Open `build/docs/html/index.html` in a browser (or run `python3 -m http.server 8080 --directory build/docs/html`).

Check:
- `physics` namespace page shows all functions with `@param`/`@return` tables
- `CollisionSystem.hpp` shows `runCollision()` with its parameter descriptions
- Struct pages (`Position`, `Velocity`, etc.) show member documentation
- No `[object Object]` or missing-doc warnings for documented items

- [ ] **Step 3: Run format check to confirm no accidentally broken formatting**

```bash
cmake --build --preset debug --target format-check
```

Expected: exit 0 (all files pass clang-format).

If it fails, run `cmake --build --preset debug --target format` to fix in-place, then re-check.

- [ ] **Step 4: Final commit**

```bash
git add build/  # only if generated docs are tracked — skip if docs/ is in .gitignore
git commit -m "docs: verify Doxygen HTML output and clang-format compliance"
```

If `build/` is gitignored (likely), the commit is just the format fixes if any:

```bash
git add src/
git commit -m "docs: fix any clang-format issues after Doxygen comment migration"
```

---

## Self-Review

**Spec coverage check:**
- ✅ Doxygen infrastructure (Doxyfile.in + cmake/Doxygen.cmake + CMakeLists.txt) — Task 1
- ✅ All 7 ECS component headers — Task 2
- ✅ Physics headers + Registry — Task 3
- ✅ Physics implementations (comment cleanup) — Task 4
- ✅ ECS system headers + implementations — Task 5
- ✅ Client class headers — Task 6
- ✅ Client implementations — Task 7
- ✅ Client system headers (including 3 stubs) — Task 8
- ✅ Server headers + implementations + 2 stubs — Task 9
- ✅ Docs generation + format verification — Task 10

**Placeholder scan:** No TBD, no "implement later", no forward references to undefined names. Stub files explicitly say `// TODO:` which is intentional (the systems don't exist yet).

**Type consistency:** All struct names, function signatures, and parameter names match across tasks.
