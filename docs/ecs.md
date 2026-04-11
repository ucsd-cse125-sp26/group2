# ECS Architecture

## 1. Architecture Overview

The engine uses [EnTT](https://github.com/skypjack/entt) as its Entity-Component-System framework. A single type alias provides the shared registry type used throughout both client and server:

```cpp
// src/ecs/registry/Registry.hpp
using Registry = entt::registry;
```

### Design principles

- **Shared physics code.** `MovementSystem` and `CollisionSystem` are compiled identically on client and server. Any divergence between the two builds is a bug because it breaks client-side prediction.
- **Components are plain structs.** No inheritance, no virtual functions, no methods beyond default constructors. Systems operate on components by querying the registry with `registry.view<...>()`.
- **Free-function systems.** Every system is a free function (or inline function) inside the `systems` namespace. Each receives the `Registry&` plus whatever per-frame state it needs (delta time, world geometry, network connection).

### Source layout

```
src/ecs/
  registry/Registry.hpp          Registry typedef
  components/                    14 component headers (shared)
  systems/
    Systems.hpp                  Namespace + placeholder update()
    MovementSystem.hpp/.cpp      Titanfall-style movement state machine
    CollisionSystem.hpp/.cpp     Swept-AABB collision + position integration
  physics/
    SweptCollision.hpp           Swept AABB queries
    WorldData.hpp                WorldGeometry (planes, boxes, brushes)

src/client/systems/              Client-only systems
  InputSampleSystem.hpp          Mouse look + keyboard sampling
  InputSendSystem.hpp            Serialise & send InputSnapshot to server

src/server/systems/              Server-only systems
  InputReceiveSystem.hpp         Deserialise client InputSnapshot into Event
  BroadcastSystem.hpp            Broadcast state to clients (stub)
```

---

## 2. Component Catalog

All components live in `src/ecs/components/`. They are plain, default-constructible structs.

### Position

```cpp
struct Position {
    glm::vec3 value{0.0f, 0.0f, 0.0f};  // Y-up, Quake-unit scale
};
```

World-space position of an entity. Written by `CollisionSystem` (position integration lives there, not in `MovementSystem`).

### PreviousPosition

```cpp
struct PreviousPosition {
    glm::vec3 value{0.0f, 0.0f, 0.0f};
};
```

Copy of `Position` captured at the start of each physics tick, used by the client renderer for inter-tick interpolation. The game loop writes it immediately before stepping physics:

```cpp
registry.view<Position, PreviousPosition>().each(
    [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });
```

Only the client needs this component. The server has no renderer and does not attach it.

### Velocity

```cpp
struct Velocity {
    glm::vec3 value{0.0f, 0.0f, 0.0f};  // units/second, Y-up
};
```

Linear velocity. Written by `MovementSystem`; consumed by `CollisionSystem` for swept integration (`pos + vel * dt`).

### CollisionShape

```cpp
struct CollisionShape {
    glm::vec3 halfExtents{16.0f, 36.0f, 16.0f};  // AABB half-dimensions
};
```

Axis-aligned bounding box centred on the entity's `Position`. The full box spans `[pos - halfExtents, pos + halfExtents]`. Defaults match a standing Quake-scale player: 32 wide, 72 tall, 32 deep.

### PlayerState

The largest component -- a full locomotion state machine state block. Read and written by `MovementSystem` and `CollisionSystem`.

```cpp
struct PlayerState {
    // ── Core state ──
    MoveMode moveMode{MoveMode::OnFoot};   // OnFoot | Sliding | WallRunning | Climbing | LedgeGrabbing
    bool grounded{false};
    bool crouching{false};
    bool sprinting{false};
    bool pendingUncrouch{false};           // deferred uncrouch after slidehop
    glm::vec3 groundNormal{0, 1, 0};

    // ── Jump state ──
    bool canDoubleJump{true};
    bool jumpedThisTick{false};            // set during the tick a jump occurs (lurch setup)
    int jumpCount{0};                      // 0 = ground, 1 = first jump, 2 = double jumped
    bool jumpHeldLastTick{false};          // edge detection
    float jumpCooldown{0.0f};

    // ── Coyote time ──
    float coyoteTimer{0.0f};               // grace time after leaving ground/wall
    bool wasGroundedLastTick{false};

    // ── Jump lurch ──
    bool jumpLurchEnabled{false};
    float jumpLurchTimer{0.0f};
    glm::vec2 moveInputsOnJump{0.0f};      // WASD direction when jump started

    // ── Sliding ──
    float slideTimer{0.0f};
    int slideFatigueCounter{0};            // diminishing returns on consecutive slidehops
    float slideBoostCooldown{0.0f};
    int slideFatigueDecayAccum{0};
    bool canEnterSlide{true};

    // ── Wallrunning ──
    WallSide wallRunSide{WallSide::None};  // None | Left | Right
    glm::vec3 wallNormal{0.0f};
    glm::vec3 wallForward{0.0f};           // direction of travel along wall
    float wallRunTimer{0.0f};
    float wallRunSpeedTimer{0.0f};
    bool exitingWall{false};
    float exitWallTimer{0.0f};
    bool wasWallRunning{false};

    // Wall blacklist: prevents immediate regrab of the same wall.
    glm::vec3 wallBlacklistNormal{0.0f};
    float wallBlacklistHeight{-1e10f};
    bool wallBlacklistActive{false};

    // ── Climbing ──
    glm::vec3 climbWallNormal{0.0f};
    float climbTimer{0.0f};
    bool exitingClimb{false};
    float exitClimbTimer{0.0f};
    bool wasClimbing{false};

    // Climb blacklist
    glm::vec3 climbBlacklistNormal{0.0f};
    float climbBlacklistHeight{-1e10f};
    bool climbBlacklistActive{false};

    // ── Ledge grabbing ──
    glm::vec3 ledgePoint{0.0f};
    glm::vec3 ledgeNormal{0.0f};
    float ledgeHoldTimer{0.0f};
    bool exitingLedge{false};
    float exitLedgeTimer{0.0f};

    // ── Grappling hook ──
    bool grappleActive{false};             // true when hook is attached and pulling
    bool grappleCooldownActive{false};
    float grappleCooldownTimer{0.0f};
    float grapplePullTimer{0.0f};
    glm::vec3 grapplePoint{0.0f};          // world-space hook anchor
    bool grappleInputLastTick{false};      // edge detection

    // ── Camera effects ──
    float targetCameraTilt{0.0f};          // target roll for wallrun lean (degrees)
};
```

**Movement mode enum:**

| Mode | Description |
|------|-------------|
| `OnFoot` | Normal ground/air movement (walk, sprint, crouch, airborne) |
| `Sliding` | Momentum slide on the ground |
| `WallRunning` | Running along a wall surface |
| `Climbing` | Climbing vertically up a wall |
| `LedgeGrabbing` | Holding onto a ledge at the top of a wall |

### InputSnapshot

```cpp
struct InputSnapshot {
    uint32_t tick{0};           // physics tick this snapshot was sampled on

    // Movement keys
    bool forward{false};        // W
    bool back{false};           // S
    bool left{false};           // A
    bool right{false};          // D
    bool jump{false};           // Space
    bool crouch{false};         // Left Ctrl
    bool sprint{false};         // Left Shift
    bool grapple{false};        // E
    bool shooting{false};

    float yaw{0.0f};           // horizontal look (radians, absolute)
    float pitch{0.0f};         // vertical look (radians, clamped to [-89 deg, +89 deg])
    float roll{0.0f};          // reserved for movement tilt

    // Previous-tick values for render interpolation of orientation
    float prevTickYaw{0.0f};
    float prevTickPitch{0.0f};
};
```

Sent from client to server each tick as a raw memcpy-serialised packet. `yaw`/`pitch` are absolute orientations (not deltas) so the server can compute `wishDir` correctly. `prevTickYaw`/`prevTickPitch` allow the renderer to interpolate orientation on the same timebase as position.

### WeaponState

```cpp
struct WeaponState {
    WeaponType current = WeaponType::Rifle;
    float fireCooldown = 0.f;   // counts down toward 0 each frame (seconds)
    int ammo = 30;
};
```

Attached to armed entities (players, bots). `WeaponType` is defined in `Projectile.hpp`.

### Renderable

```cpp
struct Renderable {
    int32_t modelIndex = -1;               // index into Renderer::models[]
    glm::vec3 scale{1.0f};                 // per-entity scale
    glm::quat orientation{1, 0, 0, 0};    // per-entity rotation (identity default)
    bool visible = true;                   // false to skip rendering
};
```

Handle into the renderer's model instance array. Attached to any entity that should be drawn in the world.

### ClientId

```cpp
struct ClientId {
    int value = -1;   // network client ID, -1 = unassigned
};
```

Associates an entity with a connected network client on the server side.

### LocalPlayer

```cpp
struct LocalPlayer {};
```

Empty marker (tag) component. Exactly one entity per client has this. `InputSampleSystem` uses it to distinguish the local player from remote player entities, ensuring remote entities' `InputSnapshot` is never overwritten by local keyboard/mouse state.

### Projectile

```cpp
enum class WeaponType : uint8_t {
    Rifle, Shotgun, Rocket, EnergyRifle, EnergySMG
};

enum class SurfaceType : uint8_t {
    Metal, Concrete, Flesh, Wood, Energy
};

struct Projectile {
    WeaponType type = WeaponType::Rifle;
    float damage = 15.f;
    entt::entity owner = entt::null;   // entity that fired this projectile
};
```

Velocity and position come from the entity's `Velocity` and `Position` components.

### ParticleEmitterTag

```cpp
enum class EmitterType : uint8_t { Smoke, Fire, Steam };

struct ParticleEmitterTag {
    EmitterType type = EmitterType::Smoke;
    float ratePerSecond = 8.f;
    float accumulator = 0.f;       // internal timer for emission spacing
    float radius = 40.f;           // spawn radius around entity position
};
```

Tag for world entities that continuously emit particles (smoke plumes, fires, steam vents). The particle system queries entities with this component each frame.

### RibbonEmitter

```cpp
struct RibbonEmitter {
    static constexpr int MaxNodes = 32;
    struct Node { glm::vec3 pos{}; float age = 0.f; };

    Node nodes[MaxNodes]{};
    int count = 0;
    int head = 0;                      // ring-buffer insertion index

    float width = 4.f;                 // half-width in world units
    float maxAge = 0.4f;               // node lifetime (seconds)
    float recordInterval = 0.016f;     // ~60 Hz recording rate
    float recordAccumulator = 0.f;

    glm::vec4 tipColor{1, 0.6, 0.1, 1};    // newest node (rocket tip)
    glm::vec4 tailColor{1, 0.3, 0.0, 0};   // oldest node (fades to transparent)
};
```

Attached to slow/arcing projectiles (rockets, slow bolts). Records a ring-buffer of historical positions. The particle system expands consecutive node pairs into camera-facing ribbon quads each frame.

### TracerEmitter

```cpp
struct TracerEmitter {
    glm::vec3 prevPos{};                               // tail anchor from previous frame
    float radius = 0.6f;                               // cross-section half-width
    glm::vec4 coreColor{1.f, 0.95f, 0.7f, 1.f};       // bright yellow-white core
    glm::vec4 edgeColor{1.f, 0.40f, 0.05f, 0.f};      // orange glow, alpha=0 at edge
};
```

Attached to fast-bullet projectile entities. The particle system reads this each frame to render an oriented-capsule streak (tip = current position, tail = `prevPos`).

---

## 3. System Execution Order

Physics runs at a fixed 128 Hz tick rate on both client and server. The client renders at an independent (usually higher) frame rate.

### Client per-tick pipeline

```
iterate() called at render frame rate
  |
  |-- runMouseLook()              [every iterate() call -- smooth camera at any FPS]
  |-- runMovementKeys()           [once per physics tick group, when synced with physics]
  |-- runInputSend()              [every iterate() call]
  |
  +-- while (accumulator >= dt):  [128 Hz fixed step, up to 8 ticks per frame]
        |
        |-- snapshot PreviousPosition = Position   [for interpolation]
        |-- runMovement(registry, dt, world)       [shared physics]
        |-- runCollision(registry, dt, world)       [shared physics]
        |
  |-- dispatcher.update()         [flush queued particle events]
  |-- particleSystem.update()     [VFX, at render rate]
  |-- render with interpolation   [alpha = accumulator / dt]
```

### Server per-tick pipeline

```
run() loop at tickRateHz (configurable, typically 128 Hz)
  |
  |-- server.poll()                                [receive network packets]
  |-- dequeue events -> eventHandler()             [write InputSnapshot from client packets]
  |-- runMovement(registry, dt, world)             [shared physics]
  |-- runCollision(registry, dt, world)            [shared physics]
  |-- (future: runBroadcast)                       [send state to clients]
```

### System call summary

| Step | Client | Server |
|------|--------|--------|
| 1 | `runMouseLook` | `runInputReceive` (via event queue) |
| 2 | `runMovementKeys` | -- |
| 3 | `runInputSend` | -- |
| 4 | `runMovement` | `runMovement` |
| 5 | `runCollision` | `runCollision` |
| 6 | `dispatcher.update` | -- |
| 7 | Render (interpolated) | `runBroadcast` (stub) |

---

## 4. Shared vs Client-Only vs Server-Only

### Shared systems (compiled identically on both sides)

| System | File | Description |
|--------|------|-------------|
| `runMovement` | `src/ecs/systems/MovementSystem.hpp` | Full Titanfall-inspired movement state machine. Computes velocity from input + state, but does NOT integrate position. |
| `runCollision` | `src/ecs/systems/CollisionSystem.hpp` | Swept-AABB collision. Integrates position (`pos + vel * dt`), clips velocity on contact, sets `grounded` flag. Quake-style multi-surface bumping (up to 4 iterations). |

Any divergence between client and server builds of these systems is a prediction-breaking bug.

### Client-only systems

| System | File | Description |
|--------|------|-------------|
| `runMouseLook` | `InputSampleSystem.hpp` | Samples SDL mouse delta, accumulates into `yaw`/`pitch`. Called every `iterate()` for smooth camera. |
| `runMovementKeys` | `InputSampleSystem.hpp` | Samples SDL keyboard state into `InputSnapshot` movement flags. Called once per physics tick group. |
| `runInputSend` | `InputSendSystem.hpp` | Serialises `InputSnapshot` and sends to server via UDP. |
| `runPrediction` | `PredictionSystem.hpp` | (Stub) Client-side prediction with ring buffer. |
| `runReconciliation` | `ReconciliationSystem.hpp` | (Stub) Rewind-and-replay on server correction. |

### Server-only systems

| System | File | Description |
|--------|------|-------------|
| `runInputReceive` | `InputReceiveSystem.hpp` | Deserialises incoming `InputSnapshot` packets into an `Event` struct for the server game loop. |
| `runBroadcast` | `BroadcastSystem.hpp` | (Stub) Serialise and broadcast ECS state to all connected clients. |

### Client-only components

| Component | Reason |
|-----------|--------|
| `PreviousPosition` | Only needed for render interpolation. |
| `LocalPlayer` | Tags the locally-controlled entity (no meaning on server). |
| `Renderable` | Links to renderer model instances. |
| `ParticleEmitterTag` | Client-side VFX. |
| `RibbonEmitter` | Client-side VFX. |
| `TracerEmitter` | Client-side VFX. |

---

## 5. Entity Archetypes

### Player entity (client)

Spawned in `Game::init()`:

```cpp
registry.emplace<Position>(player, startPos);
registry.emplace<PreviousPosition>(player, startPos);
registry.emplace<Velocity>(player);
registry.emplace<CollisionShape>(player);
registry.emplace<PlayerState>(player);
registry.emplace<InputSnapshot>(player);
registry.emplace<LocalPlayer>(player);
registry.emplace<WeaponState>(player);
registry.emplace<Renderable>(player, Renderable{.modelIndex = wraithModelIdx, .scale = glm::vec3(8.0f)});
```

| Component | Purpose |
|-----------|---------|
| `Position` | World-space location |
| `PreviousPosition` | Last-tick position for render interpolation |
| `Velocity` | Linear velocity |
| `CollisionShape` | AABB for swept collision (32x72x32 standing player) |
| `PlayerState` | Full locomotion state machine |
| `InputSnapshot` | Current tick's input |
| `LocalPlayer` | Marks as the locally controlled entity |
| `WeaponState` | Weapon type, cooldown, ammo |
| `Renderable` | Model handle for third-person rendering |

### Player entity (server)

Spawned in `ServerGame::initNewPlayer()`:

```cpp
registry.emplace<InputSnapshot>(player);
registry.emplace<Position>(player, glm::vec3{0.0f, 200.0f, 0.0f});
registry.emplace<Velocity>(player);
registry.emplace<CollisionShape>(player);
registry.emplace<PlayerState>(player);
```

| Component | Purpose |
|-----------|---------|
| `Position` | Authoritative world-space location |
| `Velocity` | Linear velocity |
| `CollisionShape` | AABB for swept collision |
| `PlayerState` | Full locomotion state machine |
| `InputSnapshot` | Last received client input |

Note: no `PreviousPosition`, `LocalPlayer`, `Renderable`, or `WeaponState`. The server has no renderer and does not distinguish a "local" player.

### Animated entity (client)

Spawned in `Game::init()` for Mixamo skeletal animation display:

```cpp
registry.emplace<Position>(animEntity, animPos);
registry.emplace<PreviousPosition>(animEntity, animPos);
registry.emplace<Renderable>(animEntity, Renderable{.modelIndex = animatedModelIdx, .scale = glm::vec3(1.0f)});
```

| Component | Purpose |
|-----------|---------|
| `Position` | World-space location |
| `PreviousPosition` | Interpolation |
| `Renderable` | Model handle (animated mesh) |

No physics components -- purely a visual entity.

### Projectile entity

Expected archetype (assembled from the component definitions):

| Component | Purpose |
|-----------|---------|
| `Position` | World-space location |
| `Velocity` | Projectile travel direction and speed |
| `Projectile` | Weapon type, damage, owner entity |
| `CollisionShape` | Hit detection AABB |
| `TracerEmitter` | (client, fast bullets) Capsule-streak VFX |
| `RibbonEmitter` | (client, rockets/slow bolts) Ribbon trail VFX |

### World particle source entity

| Component | Purpose |
|-----------|---------|
| `Position` | Emission origin |
| `ParticleEmitterTag` | Emission type (Smoke/Fire/Steam), rate, radius |

---

## 6. Data Flow

### Client frame data flow

```
  SDL keyboard/mouse
        |
        v
  InputSampleSystem
   - runMouseLook: mouse delta -> InputSnapshot.yaw/pitch
   - runMovementKeys: keyboard -> InputSnapshot.forward/back/left/right/jump/crouch/sprint/grapple
        |
        v
  InputSendSystem
   - memcpy-serialise InputSnapshot -> UDP packet to server
        |
        v
  [Physics tick loop -- 128 Hz]
        |
        |  PreviousPosition <- Position  (snapshot for interpolation)
        |
        v
  MovementSystem (runMovement)
   - Reads: InputSnapshot, PlayerState, Velocity, CollisionShape
   - Writes: Velocity, PlayerState (mode transitions, timers, camera tilt)
   - Does NOT write Position
        |
        v
  CollisionSystem (runCollision)
   - Reads: Position, Velocity, CollisionShape, PlayerState
   - Writes: Position (pos += vel * dt, clipped), PlayerState.grounded
   - Quake-style 4-iteration bump on multi-surface contact
        |
        v
  [End tick loop]
        |
        v
  Renderer
   - Reads: Position, PreviousPosition, InputSnapshot (yaw/pitch), CollisionShape, PlayerState
   - Interpolates: renderPos = mix(prev.value, pos.value, alpha)
   - alpha = accumulator / physicsDt
```

### Server tick data flow

```
  Network packets (UDP)
        |
        v
  InputReceiveSystem (runInputReceive)
   - Deserialises InputSnapshot from raw bytes -> Event struct
        |
        v
  ServerGame::eventHandler
   - Writes Event fields into entity's InputSnapshot component
        |
        v
  MovementSystem (runMovement)       [identical to client]
   - Reads: InputSnapshot, PlayerState, Velocity, CollisionShape
   - Writes: Velocity, PlayerState
        |
        v
  CollisionSystem (runCollision)     [identical to client]
   - Reads: Position, Velocity, CollisionShape, PlayerState
   - Writes: Position, PlayerState.grounded
        |
        v
  (future) BroadcastSystem
   - Would serialise Position/Velocity/PlayerState -> broadcast to all clients
```

---

## 7. Event Bus

The client uses `entt::dispatcher` as a decoupled event bus for particle/VFX events. The dispatcher is owned by the `Game` class:

```cpp
// Game.hpp
entt::dispatcher dispatcher;
```

### Event types

All defined in `src/client/particles/ParticleEvents.hpp`:

| Event | Fields | Trigger |
|-------|--------|---------|
| `WeaponFiredEvent` | `shooter`, `type`, `origin`, `direction`, `isHitscan`, `hitPos` | Left-click fires weapon |
| `ProjectileImpactEvent` | `pos`, `normal`, `surface`, `weaponType` | Projectile/hitscan hits a surface |
| `ExplosionEvent` | `pos`, `blastRadius` | Rocket/grenade detonation |

### Wiring

During `Game::init()`, event sinks are connected to `ParticleSystem` handler methods:

```cpp
dispatcher.sink<WeaponFiredEvent>().connect<&ParticleSystem::onWeaponFired>(particleSystem);
dispatcher.sink<ProjectileImpactEvent>().connect<&ParticleSystem::onImpact>(particleSystem);
dispatcher.sink<ExplosionEvent>().connect<&ParticleSystem::onExplosion>(particleSystem);
```

### Dispatch flow

1. Game code enqueues events: `dispatcher.enqueue(wfe);`
2. After physics ticks, the game flushes all queued events: `dispatcher.update();`
3. Connected handlers on `ParticleSystem` execute, spawning the appropriate VFX (tracers, impact sparks, explosions).

The event bus is client-only. The server has no particle system and no dispatcher.

---

## 8. Interpolation

### Problem

Physics runs at a fixed 128 Hz, but the renderer runs at the monitor's refresh rate (often 144+ Hz or uncapped). Without interpolation, entities visibly stutter because their positions only update every ~7.8 ms while frames render every ~4-7 ms.

### Solution: PreviousPosition + alpha blending

Each physics tick, immediately before stepping physics, the game loop snapshots the current position:

```cpp
registry.view<Position, PreviousPosition>().each(
    [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });
```

At render time, the interpolation alpha is computed from the leftover accumulator:

```cpp
float alpha = std::clamp(accumulator / physicsDt, 0.0f, 1.0f);
```

- `alpha = 0.0` means a tick just ran (render the previous tick's position).
- `alpha = 1.0` means the next tick is about to fire (render the current position).

The render position is then:

```cpp
glm::vec3 renderPos = glm::mix(prev.value, pos.value, alpha);
```

### Camera orientation interpolation

`InputSnapshot` stores `prevTickYaw` and `prevTickPitch` alongside the current `yaw`/`pitch`. This allows the renderer to interpolate orientation on the same timebase as position, preventing a visual mismatch where yaw snaps to the newest value while the eye position lags behind by up to one tick. Without this, objects jitter on screen when strafing and rotating simultaneously.

In practice, the current implementation reads `yaw`/`pitch` directly from the latest `InputSnapshot` because mouse look runs every `iterate()` call (not just at physics tick boundaries), keeping camera rotation smooth regardless.

### Sequential mode fallback

When `renderSeparateFromPhysics` is set to `false` (an ImGui toggle), the game only renders after a physics tick and uses `Position` directly with no interpolation. This caps visual frame rate at 128 Hz but eliminates any interpolation-related artifacts for debugging.
