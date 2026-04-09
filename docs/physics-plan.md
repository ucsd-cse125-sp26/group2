# Physics Implementation Plan

LAN-only (1 Gbps). Architecture: client-side prediction, server authority, no full lag compensation.

---

## Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Coordinate system | **Y-up, right-handed** | Matches GLM default and Vulkan/SDL3 GPU convention |
| Units | **Quake units** | Battle-tested constants from known games; 1 unit ≈ 1 inch |
| Movement model | **Quake PM_Accelerate** | Enables strafe jumping and bhop as emergent mechanics |
| Gravity | **1000 units/s²** | Snappy Titanfall-ish arcs; faster than real gravity by design |
| Jump speed | **380 units/s** | Jump height ≈ 72 units (~6 feet); tunable |
| Overbounce (walls) | **1.001** | Prevents corner-sticking via tiny separation impulse |
| Overbounce (floor) | **1.0** | Perfectly inelastic; no floor bouncing |
| Friction model | **Quake-style with StopSpeed** | Snappy deceleration; avoids asymptotic glide-to-stop |
| Physics timestep | **Fixed at 1/tickRateHz** | Required for deterministic reconciliation |
| Rendering | **Frame-rate independent with interpolation** | Physics at 128 Hz, render at uncapped FPS |
| Server tickrate | **128 Hz default** | Specified in Hz not ms; high-resolution timer for accuracy |
| World representation | **Planes now → AABB list → BVH+GLTF** | Incremental; clean abstraction boundary at sweepAABB |
| Jump | **Instant impulse** | Standard FPS; variable-height jump does not fit the genre |

---

## Architecture

Three layers, each depending only on the one below:

```
┌─────────────────────────────────────────────────────┐
│  SYSTEMS  (ECS-aware, queries registry)              │
│  MovementSystem, CollisionSystem          (shared)   │
│  InputSample, Prediction, Reconciliation  (client)   │
│  InputReceive, Broadcast                  (server)   │
├─────────────────────────────────────────────────────┤
│  PHYSICS  (pure math, zero ECS knowledge)            │
│  clipVelocity, sweepAABB, applyGravity, friction     │
│  PhysicsConstants — all tunable values              │
├─────────────────────────────────────────────────────┤
│  COMPONENTS  (pure data, zero logic)                 │
│  Position, Velocity, CollisionShape,                 │
│  PlayerState, InputSnapshot, PreviousPosition        │
└─────────────────────────────────────────────────────┘
```

**Key rule:** `MovementSystem` and `CollisionSystem` compile identically on client and server.
Any divergence between sides is a bug, not a design choice.

---

## Physics Constants (Quake units)

All constants live in `ecs/physics/PhysicsConstants.hpp`. Tuning is iterative — these are
starting values targeting a Titanfall-to-Quake movement feel.

| Constant | Value | Notes |
|---|---|---|
| `Gravity` | 1000 u/s² | Faster than real (~9.8 m/s²) for snappy arcs |
| `JumpSpeed` | 380 u/s | Height ≈ 72 units. Tune together with Gravity. |
| `MaxGroundSpeed` | 400 u/s | Base sprint speed |
| `GroundAccel` | 15 | How fast you reach MaxGroundSpeed |
| `AirAccel` | 2.0 | Higher than Quake (0.7) for Titanfall-ish air control |
| `AirMaxSpeed` | 30 u/s | Wish speed cap in air; does not cap momentum (bhop works) |
| `Friction` | 4.0 | Quake default |
| `StopSpeed` | 150 u/s | Friction amplified below this speed for snappy stopping |
| `StepHeight` | 18 u | Max height to auto-step over |
| `OverbounceWall` | 1.001 | Tiny separation to prevent corner sticking |
| `OverbounceFloor` | 1.0 | No bounce on floor |

---

## Fixed Timestep + Frame-Independent Rendering

Physics runs at a fixed `dt = 1.0f / tickRateHz` regardless of frame rate.
The renderer interpolates between the previous and current physics states using `alpha`.

```
Real time:    ─────────────────────────────────────────────▶
Physics ticks:  │        │        │        │        │
                t0       t1       t2       t3       t4
                                                ↑
Render frame fires here: between t3 and t4
alpha = accumulator / PHYSICS_DT  ≈  0.6
renderPos = lerp(PreviousPosition, Position, alpha)
```

**Game loop (client):**
```cpp
accumulator += frameTime;
accumulator  = std::min(accumulator, 0.25f);  // spiral-of-death guard

while (accumulator >= PHYSICS_DT) {
    savePreviousPositions(registry);           // Position → PreviousPosition
    runPhysicsTick(registry, PHYSICS_DT);
    accumulator -= PHYSICS_DT;
}

float alpha = accumulator / PHYSICS_DT;
renderer.drawFrame(registry, alpha);           // lerp(prev, cur, alpha)
```

**Server loop (128 Hz, high-resolution timer):**
```cpp
const Uint64 tickDuration = SDL_GetPerformanceFrequency() / tickRateHz;
Uint64 nextTick = SDL_GetPerformanceCounter();

while (running) {
    server.poll();
    tick(dt);

    nextTick += tickDuration;
    Uint64 now = SDL_GetPerformanceCounter();
    if (now < nextTick) {
        Sint64 sleepMs = (Sint64)((nextTick - now) * 1000 /
                          SDL_GetPerformanceFrequency()) - 1;
        if (sleepMs > 0) SDL_Delay((Uint32)sleepMs);
        while (SDL_GetPerformanceCounter() < nextTick) {}  // spin-wait <1ms
    }
}
```

The sleep-then-spin pattern: `SDL_Delay` gets close cheaply; spin-wait hits the exact boundary.
At 128 Hz the spin costs <0.1% CPU.

---

## World Representation Roadmap

The abstraction boundary is the `sweepAABB` signature. Only the world argument changes
between stages — `CollisionSystem` never needs to change.

| Stage | World type | When |
|---|---|---|
| 1 | `std::span<const Plane>` — infinite planes | Now (testing) |
| 2 | `std::span<const AABB>` — static box list | Simple handcrafted rooms |
| 3 | `BVH` over triangle mesh loaded from GLTF | Full maps from the internet |

**GLTF path (Stage 3):**
- Parse with **tinygltf** (single-header, FetchContent-friendly)
- Extract triangle soup from mesh nodes
- Build BVH at load time: tree of AABBs over triangles, built once, swept at runtime
- `sweepAABB` walks BVH: skip subtrees whose AABB the path misses → O(log N) triangle tests
- Export workflow: build map in Blender → export GLB → load at runtime

---

## File Layout

```
src/
├── ecs/
│   ├── components/
│   │   ├── Position.hpp          ✅
│   │   ├── Velocity.hpp          ✅
│   │   ├── CollisionShape.hpp    ✅
│   │   ├── PlayerState.hpp       ✅
│   │   ├── InputSnapshot.hpp     ✅
│   │   └── PreviousPosition.hpp  ✅  render interpolation only
│   ├── physics/
│   │   ├── PhysicsConstants.hpp  ✅  all tunable values
│   │   ├── Movement.hpp/.cpp     ✅  applyGravity, applyGroundFriction, accelerate, clipVelocity
│   │   └── SweptCollision.hpp/.cpp ✅ Plane, HitResult, sweepAABB
│   └── systems/
│       ├── MovementSystem.hpp/.cpp   shared
│       └── CollisionSystem.hpp/.cpp  shared
├── client/
│   └── systems/
│       ├── InputSampleSystem.hpp     SDL input → InputSnapshot component
│       ├── PredictionSystem.hpp      runs shared Movement+Collision locally
│       ├── InputSendSystem.hpp       InputSnapshot → Client::send()
│       └── ReconciliationSystem.hpp  ring buffer diff, re-sim on diverge
└── server/
    └── systems/
        ├── InputReceiveSystem.hpp    packets → InputSnapshot onto entities
        └── BroadcastSystem.hpp       Position/Velocity → Server::send()
```

---

## Steps

### Step 1 — Components ✅
**Files:** `ecs/components/*.hpp`

Plain data structs — no logic. Everything both sides agree on.

- `Position`          — `glm::vec3` world position
- `Velocity`          — `glm::vec3` units/second
- `CollisionShape`    — AABB half-extents (`glm::vec3`)
- `PlayerState`       — grounded / crouching / sliding flags
- `InputSnapshot`     — one tick of input (keys + orientation) + tick number
- `PreviousPosition`  — copy of Position before last physics step; read by renderer for interpolation

**Milestone:** compiles, entities emplaceable via `registry.emplace<Position>(e, ...)`.

---

### Step 2 — Pure Physics Math ✅
**Files:** `ecs/physics/PhysicsConstants.hpp`, `ecs/physics/Movement.hpp/.cpp`,
           `ecs/physics/SweptCollision.hpp/.cpp`

Standalone math functions — `glm::vec3` in, `glm::vec3` out. No registry, no components.

**PhysicsConstants.hpp:** all gravity, speed, accel, friction values in one place.

**Movement:**
- `applyGravity(vel, dt)` — subtract `Gravity * dt` from Y each tick
- `applyGroundFriction(vel, dt)` — Quake friction with StopSpeed threshold
- `accelerate(vel, wishDir, wishSpeed, accel, dt)` — Quake PM_Accelerate; preserves momentum
- `clipVelocity(vel, normal, overbounce)` — project velocity onto collision plane

**SweptCollision:**
- `Plane` — unit normal + distance; `dot(normal, p) > distance` means free space
- `HitResult` — hit flag, tFirst (0..1 along path), contact normal
- `sweepAABB(halfExtents, start, end, planes)` — Minkowski-sum approach; expands each
   plane by AABB extents, finds earliest ray-plane intersection along the sweep path

**Milestone:** `clipVelocity` correct for 45° wall; `sweepAABB` finds floor hit at correct t.

---

### Step 3 — MovementSystem (no collision)
**Files:** `ecs/systems/MovementSystem.hpp/.cpp`

Iterates `[Position, Velocity, PlayerState]`:
1. If not grounded: `applyGravity`
2. If grounded: `applyGroundFriction`
3. Integrate: `pos.value += vel.value * dt`

Wire into `ServerGame::tick()` and `Game::iterate()`. Spawn one test entity in `init()`.

**Milestone:** entity falls under gravity; position changes logged each tick.

---

### Step 4 — CollisionSystem (static world)
**Files:** `ecs/systems/CollisionSystem.hpp/.cpp`

Iterates `[Position, Velocity, CollisionShape, PlayerState]`:
1. Compute desired new position: `newPos = pos + vel * dt`
2. `sweepAABB(shape.halfExtents, pos, newPos, world.planes)`
3. On hit: move to contact point (`pos + tFirst * delta`), `clipVelocity`, set `grounded` if floor
4. Repeat up to 4 iterations (handles corners; Quake-style bumping)

World at this stage = hardcoded floor plane passed in. Clear `grounded` at step start,
set it only when a floor-facing normal is hit.

**Milestone:** entity falls, lands on floor, stops. No tunneling at any velocity.

---

### Step 5 — Quake Movement Feel
**Files:** `ecs/physics/Movement.cpp`, `ecs/systems/MovementSystem.cpp`

Layer feel onto working gravity + collision:
- **Ground move:** `accelerate()` toward `wishDir` (from `InputSnapshot`), Friction on no input
- **Air move:** same `accelerate()` with `AirAccel` and `AirMaxSpeed`; no friction
- **Jump:** grounded + jump input → `vel.y = JumpSpeed`, `grounded = false`
- **Crouch:** halve `CollisionShape.halfExtents.y`, set `PlayerState.crouching`
- **Stair step:** after horizontal resolution, cast down ≤ `StepHeight`; if floor found, lift entity

**Milestone:** WASD moves entity, spacebar jumps, entity slides along walls, no corner sticking.

---

### Step 6 — Client Input Pipeline
**Files:** `client/systems/InputSampleSystem.hpp`, `client/systems/InputSendSystem.hpp`

- `InputSampleSystem`: `SDL_GetKeyboardState()` + `SDL_GetRelativeMouseState()` → `InputSnapshot`
  on local player entity. Requires `SDL_SetWindowRelativeMouseMode(window, true)` in `Game::init()`.
- `InputSendSystem`: serialize `InputSnapshot` to packed struct, `Client::send()`

**Milestone:** WASD/mouse sends UDP packets; input visible in server logs.

---

### Step 7 — Server Authority
**Files:** `server/systems/InputReceiveSystem.hpp`, `server/systems/BroadcastSystem.hpp`

- `InputReceiveSystem`: drain incoming packet queue → `InputSnapshot` onto player entity
- `BroadcastSystem`: serialize `Position + Velocity + tick` per player → all clients

**Milestone:** client input moves server entity; positions broadcast back; multi-client works.

---

### Step 8 — Client Prediction
**Files:** `client/systems/PredictionSystem.hpp`

Run shared `MovementSystem` + `CollisionSystem` locally on current `InputSnapshot`, immediately.
Maintain ring buffer of `{tick, input, predictedPos, predictedVel}`.

**Milestone:** local movement instant-feeling; ring buffer fills correctly.

---

### Step 9 — Reconciliation
**Files:** `client/systems/ReconciliationSystem.hpp`

On server state packet for tick N:
1. Compare `serverPos` to `ringBuffer[N].predictedPos`
2. If `distance > threshold` (2–4 units): snap to `serverPos`, re-simulate N+1..currentTick

On LAN, re-sim window ≈ 2–5 ticks. Errors sub-unit. Optional: lerp correction over 3 frames.

**Milestone:** artificial desync snaps and re-sims without visible pop.

---

## What We Skip (LAN justification)

| Technique | Reason skipped |
|---|---|
| Full lag compensation | RTT ~1ms; current state is close enough for hit detection |
| Jitter buffers | For variable internet latency; irrelevant on LAN |
| Delta compression | Bandwidth not a constraint at 1 Gbps |
| Complex interpolation | Other players ≤ 2 ticks behind; linear interp is fine |

## What We Cannot Skip

| Technique | Why required |
|---|---|
| Swept collision | Tunneling is real at FPS movement speeds even at 128 Hz |
| Client prediction | Without it, movement feels laggy even at 1ms ping |
| Authoritative server | Without it, desyncs and cheating become the problem |
