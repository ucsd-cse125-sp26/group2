# Particle System Documentation

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Lifecycle](#2-lifecycle)
3. [Effect Types](#3-effect-types)
4. [Spawn API](#4-spawn-api)
5. [Event System](#5-event-system)
6. [GPU Data Types](#6-gpu-data-types)
7. [ParticlePool](#7-particlepool)
8. [GpuParticleBuffer](#8-gpuparticlebuffer)
9. [ParticleRenderer](#9-particlerenderer)
10. [SDF Text Rendering](#10-sdf-text-rendering)
11. [Adding a New Effect](#11-adding-a-new-effect)

---

## 1. Architecture Overview

The particle system follows a layered orchestrator pattern where `ParticleSystem` sits at the top and delegates to specialized effect subsystems and a unified GPU renderer.

### Component Hierarchy

```
ParticleSystem  (orchestrator -- owns everything below)
 |
 |-- TracerEffect          effect subsystem  (CPU simulation)
 |-- RibbonTrail           effect subsystem  (CPU simulation)
 |-- HitscanEffect         effect subsystem  (CPU simulation)
 |-- ImpactEffect          effect subsystem  (CPU simulation)
 |-- BulletHoleDecal       effect subsystem  (CPU simulation)
 |-- SmokeEffect           effect subsystem  (CPU simulation)
 |-- ExplosionEffect       effect subsystem  (CPU simulation)
 |-- SdfRenderer           text subsystem    (CPU glyph layout)
 |     '-- SdfAtlas         font/atlas owner
 |
 '-- ParticleRenderer      GPU owner (pipelines, buffers, draw calls)
       |-- GpuParticleBuffer x9  (one per data category)
       |-- SDL_GPUGraphicsPipeline x9
       |-- Shared quad index buffer
       |-- Procedural textures (smoke noise, decal atlas)
       '-- SDF atlas texture reference
```

### Data Flow Diagram

```
 Game / Weapon Logic                    entt::dispatcher
       |                                      |
       | spawnXxx() calls                     | WeaponFiredEvent
       | drawWorldText()                      | ProjectileImpactEvent
       v                                      | ExplosionEvent
+------------------+                          v
| ParticleSystem   |<--- onWeaponFired(), onImpact(), onExplosion()
|                  |
|  update(dt,cam)  |  calls each effect's update()
|        |         |  effects write into ParticlePool / std::vector
|        v         |
| uploadToGpu(cmd) |  for each category:
|        |         |    renderer_.uploadXxx(cmd, effect.data(), effect.count())
|        v         |      |
| render(pass,cmd) |      v  GpuParticleBuffer::upload()
|        |         |      memcpy to transfer buffer -> SDL_UploadToGPUBuffer
|        v         |
| renderer_        |  drawAll(pass, cmd, screenW, screenH)
|  .drawAll()      |    for each pipeline in blend-correct order:
|                  |      bind pipeline -> bind storage/vertex buf -> draw
+------------------+
```

### Key Design Decisions

- **Effects own simulation state; the renderer owns GPU state.** Effects never touch GPU resources directly. They expose `data()` / `count()` accessors returning pointers to flat arrays of GPU-uploadable structs.
- **No virtual dispatch.** Each effect is a concrete member of `ParticleSystem`; update/upload/draw order is explicit.
- **Storage buffers over vertex buffers where possible.** Most effects use `GRAPHICS_STORAGE_READ` buffers read via `gl_InstanceIndex` in the vertex shader, avoiding per-vertex attribute description boilerplate. Only ribbon trails and lightning arcs use traditional vertex buffers because they are pre-expanded triangle strips.

---

## 2. Lifecycle

### Initialization

```
Game::init()
  '-> particleSystem.init(dev, colorFmt, shaderFmt)
         |-> renderer_.init()          -- GPU buffers, pipelines, textures
         |     |-> billboardBuf_.init()   48 B * 4096 = 192 KB
         |     |-> tracerBuf_.init()      80 B *  512 =  40 KB
         |     |-> ribbonBuf_.init()      32 B * 24576 = 768 KB  (Vertex mode)
         |     |-> hitscanBuf_.init()     64 B *   64 =   4 KB
         |     |-> arcBuf_.init()         32 B * 2048 =  64 KB   (Vertex mode)
         |     |-> smokeBuf_.init()       48 B * 1024 =  48 KB
         |     |-> decalBuf_.init()       64 B *  512 =  32 KB
         |     |-> sdfWorldBuf_.init()    80 B * 4096 = 320 KB
         |     |-> sdfHudBuf_.init()      80 B * 4096 = 320 KB
         |     |-> buildQuadIndexBuffer() -- {0,1,2, 2,3,0} * 4096 instances
         |     |-> buildSmokeNoise()      -- 256x256 R8 value noise texture
         |     |-> buildDecalTexture()    -- 128x128 RGBA8 procedural bullet hole
         |     '-> buildPipelines()       -- 9 GPU pipelines
         '-> sdf_.init(dev, fontPath)  -- load TTF, bake SDF atlas, upload texture
```

### Per-Frame Update

```
Game::iterate()
  '-> particleSystem.update(dt, cam, reg)
         |-> cache camera vectors (camPos, camForward, camRight, camUp)
         |-> tracers_.update(dt, reg)          -- poll ECS entities, fade free tracers
         |-> ribbons_.update(dt, reg, camPos)  -- age nodes, expand to vertex stream
         |-> hitscan_.update(dt, camForward)   -- rebuild arc verts with fBm
         |-> smoke_.update(dt, reg, camPos, camForward)  -- drift, fade, sort B2F
         |-> impact_.update(dt)                -- gravity, fade
         |-> decals_.update(dt)                -- opacity fade
         |-> explosions_.update(dt)            -- ring expansion, pending timers
         '-> sdf_.clear()                      -- reset glyph queues
```

### Upload (Before Render Pass)

```
Renderer::drawFrame()
  '-> particleSystem.uploadToGpu(cmd)
         |-> renderer_.uploadBillboards(cmd, impact_.data(), impact_.count())
         |-> renderer_.uploadTracers(cmd, tracers_.data(), tracers_.count())
         |-> renderer_.uploadRibbon(cmd, ribbons_.data(), ribbons_.count())
         |-> renderer_.uploadHitscan(cmd, hitscan_.beamData(), hitscan_.beamCount())
         |-> renderer_.uploadArcs(cmd, hitscan_.arcData(), hitscan_.arcCount())
         |-> renderer_.uploadSmoke(cmd, smoke_.data(), smoke_.count())
         |-> renderer_.uploadDecals(cmd, decals_.data(), decals_.count())
         |-> renderer_.uploadSdfWorld(cmd, sdf_.worldData(), sdf_.worldCount())
         '-> renderer_.uploadSdfHud(cmd, sdf_.hudData(), sdf_.hudCount())
```

Each `upload` call maps a transfer buffer with `cycle=true` (avoids GPU stall from the previous frame), copies CPU data via `memcpy`, unmaps, then runs a copy pass to transfer to the GPU buffer.

### Render (Inside Render Pass)

```
  '-> particleSystem.render(pass, cmd)
         '-> renderer_.drawAll(pass, cmd, screenW, screenH)
               ordered draw calls (see Section 9 for draw order)
```

### Shutdown

```
Game::quit()
  '-> particleSystem.quit()
         |-> sdf_.quit()       -- release atlas texture/sampler
         '-> renderer_.quit()  -- wait for GPU idle, release all pipelines/buffers/textures
```

---

## 3. Effect Types

### 3.1 TracerEffect

**File:** `effects/TracerEffect.hpp`, `effects/TracerEffect.cpp`

Renders oriented capsule streaks for fast-moving bullet projectiles. Supports two modes:

**Entity-attached mode** -- a tracer follows an ECS entity with `Position`, `Velocity`, and `TracerEmitter` components. The tracer's `tip` tracks the entity position each frame; the `tail` is computed as `tip - normalize(vel) * streakLength` (180 world units). When the entity is destroyed, `detach()` sets the tracer's lifetime to `k_fadeTime` (0.1 s) and it fades out.

**Free-fire mode** -- `spawnFree(tip, tail, lifetime)` spawns a standalone streak with no ECS entity. The tracer immediately starts fading and dies after `lifetime` seconds (default 0.12 s). Used for one-shot bullet tracers.

| Parameter | Value |
|-----------|-------|
| Pool capacity | 512 |
| Streak length | 180 world units |
| Default radius | 0.6 |
| Core color (free) | (1.0, 0.95, 0.7, 1.0) -- bright yellow-white |
| Edge color (free) | (1.0, 0.40, 0.05, 0.0) -- orange, alpha=0 at edge |
| Fade time | 0.1 s |
| GPU struct | `TracerParticle` (80 bytes) |
| Blend mode | Additive |

**Entity-index tracking:** An `unordered_map<uint32_t, uint32_t>` maps entity IDs to pool indices. When `pool_.kill(i)` swaps in the last element, the map is patched so the swapped entity's index stays correct.

### 3.2 RibbonTrail

**File:** `effects/RibbonTrail.hpp`, `effects/RibbonTrail.cpp`

Builds camera-facing ribbon strips for slow/arcing projectiles (rockets). The vertex buffer is rebuilt from scratch every frame.

**Algorithm:**
1. For each entity with `Position` + `RibbonEmitter`, age all nodes and discard expired ones.
2. If enough time has elapsed since the last recording (`recordInterval`), push the current position into a ring buffer.
3. Sort surviving nodes by age (newest first).
4. Prepend the current entity position as the tip (age=0).
5. For each consecutive pair of nodes, compute a camera-facing side vector via `cross(axis, toEye)`, then emit 6 vertices (2 triangles per quad segment).
6. Color is linearly interpolated from `tipColor` to `tailColor` based on normalized age, then pre-multiplied by alpha.

| Parameter | Value |
|-----------|-------|
| Max nodes per emitter | `RibbonEmitter::MaxNodes` |
| Vertex buffer cap | 24,576 vertices (768 KB) |
| GPU struct | `RibbonVertex` (32 bytes) |
| Primitive type | Triangle list |
| Blend mode | Pre-multiplied alpha |

### 3.3 HitscanEffect

**File:** `effects/HitscanEffect.hpp`, `effects/HitscanEffect.cpp`

Renders energy beams as animated lightning arcs using fBm (fractional Brownian motion) path deviation along cubic Bezier spines. This is the most algorithmically complex effect.

**Signal chain per sample point at parameter t in [0, 1]:**
1. Optionally domain-warp t (strength 0.14, frequency 2.5) to create spatially varying frequency content.
2. Sample 4-octave fBm with lacunarity 3.0 and persistence 0.4 (primary axis), and 3-octave fBm (secondary axis, 45% amplitude).
3. Multiply by `sin(t * pi)` envelope so the arc is pinned at both endpoints.
4. Scale by `baseAmplitude` = 16% of beam length.
5. Apply displacement perpendicular to the Bezier tangent.

**Rendering:** Three concentric triangle-strip layers per arc:

| Layer | Radius | Color | Alpha | Purpose |
|-------|--------|-------|-------|---------|
| Outer bloom | 6.5 | (0.20, 0.50, 1.00) | 0.07 * fade | Atmospheric scatter halo |
| Inner glow | 1.8 | (0.45, 0.80, 1.00) | 0.55 * fade | Visible arc body |
| Core | 0.4 | (0.92, 0.97, 1.00) | 0.96 * fade | White-hot centerline |

**Temporal behavior:**
- **Smooth Bezier morph:** When a new shot fires, control points interpolate from previous to new shape over 80 ms.
- **Continuous animation:** Each fBm octave runs at 2x the speed of the one below, giving slow large-scale breathing overlaid with fast fine crackle.
- **Return strokes:** 2-3 strokes scheduled at 60/120/180 ms after the initial shot. Each re-randomizes the arc path and branches.
- **Branch flicker:** Branches re-randomize every 45 ms. Each branch has 2-octave fBm with its own seed.

**Branches:** 2-5 forked branches diverge from random points along the main arc:
- `tStart`: 0.10-0.85 along the main arc
- `length`: 7-24% of main bolt length
- `angle`: +/-30 degrees from tangent
- Rendered as 2-layer strips (glow at radius 0.90, core at radius 0.22)

| Parameter | Value |
|-----------|-------|
| Max concurrent beams | 4 |
| Bezier segments | 32 |
| Branch segments | 7 |
| Max branches per beam | 5 |
| Beam lifetime | 0.22 s |
| Interpolation duration | 0.08 s |
| Branch re-randomize interval | 0.045 s |
| GPU struct | `ArcVertex` (32 bytes) via vertex buffer |
| Arc vertex buffer cap | 2048 |
| Blend mode | Additive |

### 3.4 ImpactEffect

**File:** `effects/ImpactEffect.hpp`, `effects/ImpactEffect.cpp`

Spawns spark bursts and an impact flash billboard when a projectile hits a surface. Spark count, color, velocity, and size depend on the `SurfaceType`.

**Per-surface parameters:**

| Surface | Color | Speed | Size | Count |
|---------|-------|-------|------|-------|
| Metal | (1.0, 0.90, 0.60) white-yellow | 300-600 | 0.8-1.5 | 14 |
| Concrete | (0.7, 0.65, 0.60) grey | 150-350 | 1.2-2.5 | 10 |
| Flesh | (0.8, 0.10, 0.10) dark red | 100-250 | 1.5-3.0 | 8 |
| Wood | (0.6, 0.40, 0.20) brown | 120-300 | 1.0-2.0 | 10 |
| Energy | (0.2, 0.80, 1.00) cyan | 400-700 | 0.5-1.2 | 16 |

**Spark physics:**
- Direction: random within a 55-degree half-angle cone around the surface normal.
- Gravity: 1000 units/s^2 downward.
- Lifetime: 0.25-0.45 s.
- Alpha fade: quadratic `1 - ((1 - lifetime/0.45)^2)`.

**Impact flash:** A single oversized billboard (size=20) with color (1.0, 0.95, 0.8, 1.0) that lives exactly one frame (`frameDt + 0.001`).

| Parameter | Value |
|-----------|-------|
| Pool capacity | 4096 `BillboardParticle` |
| Gravity | 1000 units/s^2 |
| Cone half-angle | 55 degrees |
| GPU struct | `BillboardParticle` (48 bytes) |
| Blend mode | Additive |

### 3.5 BulletHoleDecal

**File:** `effects/BulletHoleDecal.hpp`, `effects/BulletHoleDecal.cpp`

Projects world-space decals onto surfaces. Uses a ring-buffer of 512 fixed slots -- when full, the oldest decal is overwritten. All 512 slots are uploaded every frame (32 KB).

**Spawn logic:**
- Computes tangent (`right`) and bitangent (`up`) from the hit normal.
- Size is 4.0 for rifles, 30.0 for rockets.
- UV region covers the full procedural decal texture.

**Fade:** Linear opacity decay from 1.0 to 0.0 over 15 seconds (`opacity -= dt / 15.0`).

| Parameter | Value |
|-----------|-------|
| Capacity | 512 (ring buffer, no kill/compact) |
| Rifle decal size | 4.0 world units |
| Rocket decal size | 30.0 world units |
| Fade duration | 15 s |
| Decal texture | 128x128 RGBA8, procedural |
| GPU struct | `DecalInstance` (64 bytes) |
| Blend mode | Alpha blend |
| Depth bias | Yes (-1.0 constant, -1.0 slope) |

### 3.6 SmokeEffect

**File:** `effects/SmokeEffect.hpp`, `effects/SmokeEffect.cpp`

Volumetric smoke billboards using a noise texture for visual detail. Supports one-shot spawning and continuous emission via `ParticleEmitterTag` ECS entities.

**Spawn:** Creates a cluster of 8 smoke puffs (or 4 for fire mode) distributed randomly within a radius.

**Simulation:**
- Upward drift: 18 units/s.
- Size growth: asymptotic approach from initial (25-50) toward 120 units.
- Rotation: slow random spin at 0.15 rad/s.
- Alpha envelope: fade in 0 to 0.35 during the first 20% of life, hold at 0.35, fade out 0.35 to 0 during the last 30%.
- Colors are pre-multiplied by alpha before upload.

**Sorting:** Particles are sorted back-to-front each frame by dot product with the camera forward vector for correct alpha layering.

**Continuous emitters:** Entities with `ParticleEmitterTag` spawn smoke at `ratePerSecond` using an accumulator pattern.

| Parameter | Value |
|-----------|-------|
| Pool capacity | 1024 |
| Up drift | 18 units/s |
| Initial size | 25-50 |
| Max size | 120 |
| Max lifetime | 3-5 s |
| Rotation speed | 0.15 rad/s |
| Smoke color | (0.4, 0.4, 0.4) grey |
| Fire color | (0.9, 0.4, 0.05) orange |
| Noise texture | 256x256 R8_UNORM, 4-octave value noise |
| GPU struct | `SmokeParticle` (48 bytes) |
| Blend mode | Pre-multiplied alpha |

### 3.7 ExplosionEffect

**File:** `effects/ExplosionEffect.hpp`, `effects/ExplosionEffect.cpp`

Multi-phase explosion combining shockwave rings, immediate fireballs, and deferred smoke clouds.

**Phase 1 -- Shockwave ring:** A single `BillboardParticle` whose size starts at 0 and ramps to `blastRadius * 1.5` over 0.3 s. The fragment shader interprets this as a ring shape. Alpha fades quadratically: `1 - progress^2`.

**Phase 2 -- Immediate fireballs:** Two clusters of fire-mode smoke puffs spawned via `SmokeEffect::spawn()` at 40% and 30% of blast radius.

**Phase 3 -- Deferred smoke cloud:** A pending smoke spawn scheduled at 0.1 s delay, at 80% of blast radius. The `pending_` array holds up to 32 deferred spawns.

| Parameter | Value |
|-----------|-------|
| Ring pool capacity | 64 |
| Ring lifetime | 0.3 s |
| Ring target size | blastRadius * 1.5 |
| Max pending smoke | 32 |
| Smoke delay | 0.1 s |
| GPU struct | `BillboardParticle` (48 bytes, reused) |
| Blend mode | Additive (ring), pre-multiplied alpha (smoke) |

---

## 4. Spawn API

All spawn methods are on `ParticleSystem`. Call from weapon logic, game events, or ECS systems.

### Direct Spawn Methods

```cpp
// Attach an oriented-capsule tracer to a projectile entity.
// Adds TracerEmitter component if not present.
void spawnProjectileTracer(entt::entity e, Registry& reg);

// Attach a ribbon trail to a projectile entity.
// Adds RibbonEmitter component if not present.
void spawnRibbonTrail(entt::entity e, Registry& reg);

// One-shot bullet tracer streak (no entity required).
// tip = origin + dir * range, tail = origin.
void spawnBulletTracer(glm::vec3 origin, glm::vec3 dir, float range = 500.f);

// Instant energy beam from origin to hitPos.
void spawnHitscanBeam(glm::vec3 origin, glm::vec3 hitPos, WeaponType wt);

// Spark burst + impact flash + bullet hole decal.
void spawnImpactEffect(glm::vec3 pos, glm::vec3 normal, SurfaceType surf, WeaponType wt);

// Standalone bullet hole decal.
void spawnBulletHole(glm::vec3 pos, glm::vec3 normal, WeaponType wt);

// Smoke cloud at pos.
void spawnSmoke(glm::vec3 pos, float radius);

// Full rocket explosion (ring + fireball + deferred smoke).
void spawnExplosion(glm::vec3 pos, float blastRadius);
```

### SDF Text Methods

```cpp
// World-space billboard text (depth-tested, camera-facing).
void drawWorldText(glm::vec3 worldPos, std::string_view text,
                   glm::vec4 color, float worldHeight);

// Screen-space HUD text in pixel coordinates (no depth test).
void drawScreenText(glm::vec2 pixelPos, std::string_view text,
                    glm::vec4 color, float pixelHeight);
```

Text methods queue glyphs for the current frame only -- call them every frame the text should be visible.

### Debug Count Accessors

```cpp
uint32_t impactCount() const;
uint32_t tracerCount() const;
uint32_t ribbonVertexCount() const;
uint32_t hitscanBeamCount() const;
uint32_t arcVertexCount() const;
uint32_t smokeCount() const;
uint32_t decalCount() const;
bool     sdfReady() const;
```

---

## 5. Event System

The particle system integrates with `entt::dispatcher` for decoupled spawning from gameplay code.

### Event Structs

All defined in `ParticleEvents.hpp`:

```cpp
struct WeaponFiredEvent {
    entt::entity shooter;     // Firing entity
    WeaponType   type;        // Rifle, Rocket, etc.
    glm::vec3    origin;      // Muzzle world position
    glm::vec3    direction;   // Normalized fire direction
    bool         isHitscan;   // true for instant-hit weapons
    glm::vec3    hitPos;      // Valid only when isHitscan == true
};

struct ProjectileImpactEvent {
    glm::vec3   pos;          // World-space impact position
    glm::vec3   normal;       // Surface normal at impact
    SurfaceType surface;      // Metal, Concrete, Flesh, Wood, Energy
    WeaponType  weaponType;   // Source weapon
};

struct ExplosionEvent {
    glm::vec3 pos;            // World-space center
    float     blastRadius;    // Default 100.0
};
```

### Handler Registration

Register the three handlers on an `entt::dispatcher`:

```cpp
dispatcher.sink<WeaponFiredEvent>().connect<&ParticleSystem::onWeaponFired>(particleSystem);
dispatcher.sink<ProjectileImpactEvent>().connect<&ParticleSystem::onImpact>(particleSystem);
dispatcher.sink<ExplosionEvent>().connect<&ParticleSystem::onExplosion>(particleSystem);
```

### Handler Behavior

| Event | Handler | Action |
|-------|---------|--------|
| `WeaponFiredEvent` | `onWeaponFired` | If `isHitscan`, calls `spawnHitscanBeam(origin, hitPos, type)` |
| `ProjectileImpactEvent` | `onImpact` | Calls `spawnImpactEffect(pos, normal, surface, weaponType)` |
| `ExplosionEvent` | `onExplosion` | Calls `spawnExplosion(pos, blastRadius)` |

---

## 6. GPU Data Types

All structs defined in `ParticleTypes.hpp`. They satisfy std430 alignment (vec4-aligned fields). Static assertions enforce exact sizes.

### BillboardParticle (48 bytes)

Used by: ImpactEffect (sparks, flash), ExplosionEffect (shockwave ring).

```
Offset  Size  Field       Description
  0      12   pos         World-space center (vec3)
 12       4   size        Half-extent of camera-facing quad (float)
 16      16   color       RGBA; alpha drives additive brightness (vec4)
 32      12   vel         CPU-simulated velocity for gravity (vec3)
 44       4   lifetime    Seconds remaining; particle dies at <= 0 (float)
```

### TracerParticle (80 bytes)

Used by: TracerEffect (bullet streaks).

```
Offset  Size  Field       Description
  0      12   tip         World-space front of streak (vec3)
 12       4   radius      Cross-section half-width, ~0.6 (float)
 16      12   tail        World-space back of streak (vec3)
 28       4   brightness  Fade 1.0 -> 0.0 at end of life (float)
 32      16   coreColor   Bright yellow-white core (vec4)
 48      16   edgeColor   Orange outer edge, alpha=0 (vec4)
 64       4   lifetime    Seconds remaining (float)
 68      12   _pad        Padding to 80 bytes
```

### RibbonVertex (32 bytes)

Used by: RibbonTrail (rocket trails). Uploaded as a flat vertex stream.

```
Offset  Size  Field       Description
  0      12   pos         World-space vertex position (vec3)
 12       4   _p          Padding (float)
 16      16   color       Pre-multiplied alpha RGBA (vec4)
```

### HitscanBeam (64 bytes)

Used by: HitscanEffect (beam quad -- currently unused; arcs render instead).

```
Offset  Size  Field       Description
  0      12   origin      Muzzle world position (vec3)
 12       4   radius      Half-width, ~1.5 (float)
 16      12   hitPos      Impact world position (vec3)
 28       4   lifetime    Fades quadratically over ~0.12 s (float)
 32      16   coreColor   Cyan-white: (0.5, 0.9, 1.0, 1.0) (vec4)
 48      16   edgeColor   Deep blue: (0.0, 0.3, 0.8, 0.0) (vec4)
```

### ArcVertex (32 bytes)

Used by: HitscanEffect (lightning arcs). Pre-expanded triangle strip.

```
Offset  Size  Field       Description
  0      12   pos         World-space vertex position (vec3)
 12       4   edge        0 = centerline, +/-1 = outer edge (float)
 16      16   color       RGBA color (vec4)
```

### SmokeParticle (48 bytes)

Used by: SmokeEffect (volumetric smoke/fire billboards).

```
Offset  Size  Field            Description
  0      12   pos              World-space center (vec3)
 12       4   size             Grows from 30 -> 120 units (float)
 16      16   color            Pre-multiplied alpha RGBA (vec4)
 32       4   rotation         Slow random spin, 0.1-0.3 rad/s (float)
 36       4   normalizedAge    0 (spawned) -> 1 (dying) (float)
 40       4   maxLifetime      3-5 s (float)
 44       4   _pad             Padding (float)
```

### DecalInstance (64 bytes)

Used by: BulletHoleDecal (bullet holes, scorch marks).

```
Offset  Size  Field       Description
  0      12   pos         World-space center (vec3)
 12       4   size        Half-extent in world units, ~4 (float)
 16      12   right       World-space tangent from hit normal (vec3)
 28       4   _p0         Padding (float)
 32      12   up          World-space bitangent (vec3)
 44       4   opacity     Fades 1.0 -> 0.0 over ~15 s (float)
 48       8   uvMin       Atlas UV min (vec2)
 56       8   uvMax       Atlas UV max (vec2)
```

### SdfGlyphGPU (80 bytes)

Used by: SdfRenderer (world-space and HUD text).

```
Offset  Size  Field       Description
  0      12   worldPos    Bottom-left corner, world or pixel space (vec3)
 12       4   size        Glyph height in world units or pixels (float)
 16       8   uvMin       Atlas UV top-left (vec2)
 24       8   uvMax       Atlas UV bottom-right (vec2)
 32      16   color       RGBA (vec4)
 48      12   right       camRight for world, (1,0,0) for HUD (vec3)
 60       4   _p0         Padding (float)
 64      12   up          camUp for world, (0,1,0) for HUD (vec3)
 76       4   _p1         Padding (float)
```

---

## 7. ParticlePool

**File:** `ParticlePool.hpp`

A fixed-capacity, contiguous-array particle pool with O(1) swap-remove. Used by `TracerEffect`, `ImpactEffect`, `SmokeEffect`, and `ExplosionEffect`.

### Template Parameters

```cpp
template <typename T, uint32_t MaxN>
struct ParticlePool;
```

- `T` -- the particle struct type (e.g., `BillboardParticle`).
- `MaxN` -- compile-time maximum capacity.

### Data Layout

```
data[0] data[1] ... data[count-1] | (unused slots) ... data[MaxN-1]
  ^---- live particles ----^
```

Live particles occupy `data[0..count-1]`. Order is NOT stable -- `kill()` swaps in the last element. This is intentional: no sort is needed for GPU upload since particles are rendered without ordering requirements (additive blend is commutative).

### API

| Method | Complexity | Description |
|--------|------------|-------------|
| `spawn()` | O(1) | Zero-initializes `data[count]`, returns pointer; returns `nullptr` when full. |
| `kill(i)` | O(1) | `data[i] = data[--count]` -- swap-remove. Does NOT preserve order. |
| `update(fn)` | O(n) | Iterates backwards. `fn(T&) -> bool`: return false to kill. Backward iteration ensures `kill()` inside `fn` does not skip elements. |
| `rawData()` | O(1) | Returns `const T*` to the contiguous live data for GPU upload. |
| `liveCount()` | O(1) | Returns number of live particles. |
| `empty()` | O(1) | True when `count == 0`. |
| `full()` | O(1) | True when `count >= MaxN`. |

### Usage Pattern

```cpp
// Spawn
auto* p = pool.spawn();
if (!p) return; // pool full
p->pos = ...;
p->lifetime = 0.5f;

// Update each frame
pool.update([&](Particle& p) -> bool {
    p.lifetime -= dt;
    if (p.lifetime <= 0.f)
        return false;  // kill this particle
    p.pos += p.vel * dt;
    return true;       // keep alive
});

// Upload to GPU
renderer.upload(cmd, pool.rawData(), pool.liveCount());
```

---

## 8. GpuParticleBuffer

**File:** `GpuParticleBuffer.hpp`, `GpuParticleBuffer.cpp`

Manages a GPU buffer + CPU transfer buffer pair for uploading particle data each frame.

### Two Modes

| Mode | GPU buffer usage flag | Binding method | Used by |
|------|----------------------|----------------|---------|
| **Storage** (default) | `GRAPHICS_STORAGE_READ` | `bindAsVertexStorage(pass, slot)` | billboards, tracers, hitscan, smoke, decals, SDF |
| **Vertex** | `VERTEX` | `bindAsVertex(pass)` | ribbon trails, lightning arcs |

**Storage mode:** Vertex shaders read particle data as a readonly storage buffer via `gl_InstanceIndex`. No vertex attribute description is needed. The vertex shader generates quad corners procedurally.

**Vertex mode:** For pre-expanded flat vertex streams. The vertex shader reads per-vertex attributes through standard vertex input bindings.

### Upload Flow

```
upload(cmd, data, count, stride) called before render pass
  |
  |  1. Map transfer buffer with cycle=true
  |     (SDL_GPU internally rotates staging memory so the GPU can still
  |      read from the previous frame's upload without stalling)
  |
  |  2. memcpy(mapped, data, count * stride)
  |
  |  3. Unmap transfer buffer
  |
  |  4. Begin a copy pass:
  |       SDL_BeginGPUCopyPass(cmd)
  |       SDL_UploadToGPUBuffer(cp, &src, &dst, cycle=false)
  |       SDL_EndGPUCopyPass(cp)
  |
  '  liveCount_ = count  (used by draw calls to know how many to render)
```

### Buffer Sizes

| Buffer | Struct | Mode | Max elements | Bytes |
|--------|--------|------|-------------|-------|
| billboardBuf_ | BillboardParticle | Storage | 4096 | 192 KB |
| tracerBuf_ | TracerParticle | Storage | 512 | 40 KB |
| ribbonBuf_ | RibbonVertex | Vertex | 24,576 | 768 KB |
| hitscanBuf_ | HitscanBeam | Storage | 64 | 4 KB |
| arcBuf_ | ArcVertex | Vertex | 2048 | 64 KB |
| smokeBuf_ | SmokeParticle | Storage | 1024 | 48 KB |
| decalBuf_ | DecalInstance | Storage | 512 | 32 KB |
| sdfWorldBuf_ | SdfGlyphGPU | Storage | 4096 | 320 KB |
| sdfHudBuf_ | SdfGlyphGPU | Storage | 4096 | 320 KB |

---

## 9. ParticleRenderer

**File:** `ParticleRenderer.hpp`, `ParticleRenderer.cpp`

Owns all GPU pipelines, textures, and per-category `GpuParticleBuffer` instances.

### Pipeline Creation

Two factory methods build pipelines:

**`makeStoragePipeline()`** -- For storage-buffer-based effects. Vertex shader declares a push uniform block (slot 0) and reads one or more storage buffers. Configurable parameters: blend state, depth test/write, depth bias, primitive type.

**`makeVertexPipeline()`** -- For vertex-buffer-based effects. Requires an explicit `SDL_GPUVertexInputState` describing vertex attributes and bindings.

Both factories:
- Load SPIR-V (or MSL on macOS) shaders from the `shaders/` directory.
- Configure a single color target with the swapchain format.
- Set depth/stencil target to `D32_FLOAT`.
- Disable face culling (`CULLMODE_NONE`) since particles are camera-facing.
- Release shader modules immediately after pipeline creation.

### Pipeline Table

| # | Pipeline | Shaders | Buffer Mode | Blend | Depth Test | Depth Write | Depth Bias | Primitive |
|---|----------|---------|-------------|-------|------------|-------------|------------|-----------|
| 1 | billboardPipeline_ | particle_billboard.vert/frag | Storage (1 buf) | Additive | Yes | No | No | Triangle list |
| 2 | tracerPipeline_ | tracer.vert/frag | Storage (1 buf) | Additive | Yes | No | No | Triangle list |
| 3 | ribbonPipeline_ | ribbon.vert/frag | Vertex (2 attrs) | Premul alpha | Yes | No | No | Triangle list |
| 4 | hitscanPipeline_ | hitscan_beam.vert/frag | Storage (1 buf) | Additive | Yes | No | No | Triangle list |
| 5 | arcPipeline_ | lightning_arc.vert/frag | Vertex (2 attrs) | Additive | Yes | No | No | Triangle strip |
| 6 | smokePipeline_ | smoke.vert/frag | Storage (1 buf, 1 samp) | Premul alpha | Yes | No | No | Triangle list |
| 7 | decalPipeline_ | decal.vert/frag | Storage (1 buf, 1 samp) | Alpha | Yes | No | Yes | Triangle list |
| 8 | sdfWorldPipeline_ | sdf_text.vert/frag | Storage (1 buf, 1 samp) | Alpha | Yes | No | No | Triangle list |
| 9 | sdfHudPipeline_ | sdf_text.vert/frag | Storage (1 buf, 1 samp) | Alpha | No | No | No | Triangle list |

### Blend States

| Name | Src Color | Dst Color | Src Alpha | Dst Alpha | Use |
|------|-----------|-----------|-----------|-----------|-----|
| Additive | SRC_ALPHA | ONE | ONE | ZERO | Sparks, tracers, beams, arcs |
| Premul Alpha | ONE | 1 - SRC_ALPHA | ONE | 1 - SRC_ALPHA | Ribbons, smoke |
| Alpha | SRC_ALPHA | 1 - SRC_ALPHA | ONE | 1 - SRC_ALPHA | Decals, SDF text |

### Draw Order

`drawAll()` issues draw calls in this fixed order to handle transparency correctly:

| Order | What | Why this position |
|-------|------|-------------------|
| 1 | Decals | Depth-biased alpha blend; must go first so sparks overlay them |
| 2 | Capsule tracers | Additive; order-independent |
| 3 | Ribbon trails | Premul alpha; drawn over decals but under additive effects |
| 4 | Hitscan beams | Additive; order-independent |
| 5 | Lightning arcs | Additive; order-independent |
| 6 | Billboard sparks | Additive; order-independent |
| 7 | Smoke | Premul alpha; must be after additive effects so smoke occludes glow correctly; particles are CPU-sorted back-to-front |
| 8 | World SDF text | Alpha blend with depth test |
| 9 | HUD SDF text | Alpha blend, no depth; last so it overlays everything. Uses an orthographic projection pushed as a uniform. |

### Shared Quad Index Buffer

A pre-built index buffer containing `{0,1,2, 2,3,0}` repeated for up to 4096 quad instances (49,152 bytes). All storage-mode effects use instanced indexed draws: the vertex shader generates 4 corners per instance from the storage buffer data, and the index buffer selects the correct triangle winding.

### Procedural Textures

**Smoke noise** -- 256x256 `R8_UNORM`. 4-octave value noise with smoothstep interpolation, tiling. Linear filter, repeat wrapping.

**Decal atlas** -- 128x128 `R8G8B8A8_UNORM`. Procedural bullet hole with concentric zones:
- Solid black center (r < 14% of size)
- Dark crater (r < 22%)
- Scorch ring with heat tint (r < 34%)
- Outer scorch fade with soot (r < 46%)
- Transparent outside

All boundaries are roughened by procedural noise for irregular edges.

### ParticleUniforms

A uniform block pushed before every draw call (pushed from the main renderer, not shown in particle code):

```cpp
struct alignas(16) ParticleUniforms {
    glm::mat4 view;       // Camera view matrix
    glm::mat4 proj;       // Camera projection matrix
    glm::vec3 camPos;     // Camera world position
    float     _p0;
    glm::vec3 camRight;   // Camera right vector
    float     _p1;
    glm::vec3 camUp;      // Camera up vector
    float     _p2;
};
```

For HUD text, the renderer pushes an orthographic projection (`glm::ortho(0, screenW, 0, screenH, -1, 1)`) with an identity view matrix.

---

## 10. SDF Text Rendering

### Overview

The SDF (Signed Distance Field) text system renders resolution-independent text in both world-space (billboarded, depth-tested) and screen-space (HUD overlay) modes.

### Components

```
SdfRenderer
  |-- SdfAtlas      (font loading, SDF baking, GPU texture)
  |     '-- uses stb_truetype for glyph rasterization
  |-- worldGlyphs_  (std::vector<SdfGlyphGPU>, rebuilt each frame)
  '-- hudGlyphs_    (std::vector<SdfGlyphGPU>, rebuilt each frame)
```

### SdfAtlas -- Glyph Rasterization

1. **Font loading:** Loads a TTF file via `SDL_LoadFile`, initializes with `stb_truetype`.
2. **Glyph rendering:** For ASCII 32-126 (95 printable characters):
   - Rasterize at 48 px height using `stbtt_MakeCodepointBitmapSubpixel`.
   - Pad the bitmap by `spread` pixels (8 px) on all sides.
3. **SDF baking:** Brute-force nearest-edge scan within the spread window. For each pixel, find the minimum distance to a boundary (inside/outside transition). Output is normalized to [0, 1] where 0.5 = on the edge.
4. **Atlas packing:** Shelf-packing algorithm with 2 px padding between glyphs. Atlas size is 1024x1024 `R8_UNORM`.
5. **GPU upload:** Transfer buffer -> copy pass -> GPU texture. Linear-clamp sampler.

### GlyphInfo

```cpp
struct GlyphInfo {
    glm::vec2 uvMin;    // Atlas UV top-left
    glm::vec2 uvMax;    // Atlas UV bottom-right
    glm::vec2 bearing;  // Offset from cursor to glyph top-left
    float     advance;  // Horizontal cursor advance
    float     width;    // Glyph pixel width at bake size
    float     height;   // Glyph pixel height at bake size
};
```

### SdfRenderer -- Text Layout

**World text (`drawWorldText`):**
- Scales glyph metrics from bake pixels to world units: `scale = worldHeight / 48`.
- Positions each glyph along `camRight`, offset by bearing, with `camUp` for vertical placement.
- Sets `right` and `up` to camera basis vectors for billboarding.

**Screen text (`drawScreenText`):**
- Scales glyph metrics to pixel units: `scale = pixelHeight / 48`.
- Packs pixel coordinates into `worldPos.xy` (z=0).
- Sets `right = (1,0,0)` and `up = (0,1,0)` (axis-aligned).

### Font Discovery

At init, the system scans a priority list of system font paths:
1. `/usr/share/fonts/Adwaita/AdwaitaSans-Regular.ttf` (Fedora / GNOME)
2. `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` (Debian/Ubuntu)
3. `/usr/share/fonts/TTF/DejaVuSans.ttf` (Arch)
4. `/usr/share/fonts/noto/NotoSans-Regular.ttf` (Noto)
5. `/usr/share/fonts/liberation/LiberationSans-Regular.ttf`
6. `/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf`
7. `/System/Library/Fonts/Helvetica.ttc` (macOS)
8. `C:/Windows/Fonts/segoeui.ttf` (Windows)

The first font that loads successfully is used. If none are found, text rendering is silently disabled.

### Rendering Pipeline

Two separate GPU pipelines share the same shader pair (`sdf_text.vert` / `sdf_text.frag`):
- **World:** Alpha blend, depth test enabled.
- **HUD:** Alpha blend, depth test disabled. Orthographic projection pushed as uniform.

---

## 11. Adding a New Effect

Step-by-step guide for adding a new particle effect type.

### Step 1: Define the GPU Struct

Add a new struct to `ParticleTypes.hpp`:

```cpp
struct MyParticle {
    glm::vec3 pos;
    float     size;
    glm::vec4 color;
    float     lifetime;
    float     _pad[3]; // pad to multiple of 16
};
static_assert(sizeof(MyParticle) == 48); // enforce layout
```

Ensure std430 alignment: vec4 fields at 16-byte boundaries, total size a multiple of 16.

### Step 2: Create the Effect Class

Create `effects/MyEffect.hpp` and `effects/MyEffect.cpp`:

```cpp
// MyEffect.hpp
#pragma once
#include "particles/ParticlePool.hpp"
#include "particles/ParticleTypes.hpp"

class MyEffect {
public:
    void update(float dt);
    void spawn(glm::vec3 pos, /* params */);

    [[nodiscard]] const MyParticle* data() const { return pool_.rawData(); }
    [[nodiscard]] uint32_t count() const { return pool_.liveCount(); }

private:
    ParticlePool<MyParticle, 1024> pool_;
};
```

```cpp
// MyEffect.cpp
#include "MyEffect.hpp"

void MyEffect::spawn(glm::vec3 pos, /* params */) {
    auto* p = pool_.spawn();
    if (!p) return;
    p->pos = pos;
    p->lifetime = 1.0f;
    // ...
}

void MyEffect::update(float dt) {
    pool_.update([&](MyParticle& p) -> bool {
        p.lifetime -= dt;
        return p.lifetime > 0.f;
    });
}
```

### Step 3: Write Shaders

Create `shaders/my_effect.vert` and `shaders/my_effect.frag`. For storage-mode effects, the vertex shader reads from a storage buffer:

```glsl
// Vertex shader reads particle data via gl_InstanceIndex
// and generates 4 quad corners (gl_VertexIndex 0-3).
```

Compile to SPIR-V and place in the `shaders/` directory.

### Step 4: Register in ParticleRenderer

1. Add a `GpuParticleBuffer` member and `SDL_GPUGraphicsPipeline*` member:

```cpp
// ParticleRenderer.hpp
GpuParticleBuffer myBuf_;
SDL_GPUGraphicsPipeline* myPipeline_ = nullptr;
```

2. Initialize the buffer in `init()`:

```cpp
myBuf_.init(dev, sizeof(MyParticle) * 1024);
```

3. Build the pipeline in `buildPipelines()`:

```cpp
myPipeline_ = makeStoragePipeline(
    "my_effect.vert", "my_effect.frag",
    1, 0,              // 1 storage buffer, 0 samplers
    additiveBlend(),   // or premulAlphaBlend() or alphaBlend()
    true, false, false // depth test, no depth write, no depth bias
);
```

4. Add upload method:

```cpp
void uploadMy(SDL_GPUCommandBuffer* cmd, const MyParticle* d, uint32_t n) {
    myBuf_.upload(cmd, d, n, sizeof(MyParticle));
}
```

5. Add draw call in `drawAll()` at the appropriate position in the blend order:

```cpp
if (myPipeline_ && myBuf_.liveCount() > 0) {
    SDL_BindGPUGraphicsPipeline(pass, myPipeline_);
    bindIndex();
    myBuf_.bindAsVertexStorage(pass, 0);
    drawQuads(myBuf_.liveCount());
}
```

6. Release resources in `quit()`:

```cpp
relPL(myPipeline_);
myBuf_.quit();
```

### Step 5: Register in ParticleSystem

1. Add the effect member:

```cpp
// ParticleSystem.hpp
#include "effects/MyEffect.hpp"
MyEffect myEffect_;
```

2. Add a spawn method:

```cpp
void spawnMyEffect(glm::vec3 pos, /* params */);
```

3. Wire into the lifecycle:

```cpp
// update()
myEffect_.update(dt);

// uploadToGpu()
renderer_.uploadMy(cmd, myEffect_.data(), myEffect_.count());
```

4. Optionally add a debug count accessor:

```cpp
[[nodiscard]] uint32_t myEffectCount() const { return myEffect_.count(); }
```

### Step 6: Add Event Integration (Optional)

If the effect should respond to game events:

1. Add a new event struct to `ParticleEvents.hpp`.
2. Add a handler method to `ParticleSystem`.
3. Register on the dispatcher:

```cpp
dispatcher.sink<MyEvent>().connect<&ParticleSystem::onMyEvent>(particleSystem);
```

### Summary Checklist

- [ ] GPU struct in `ParticleTypes.hpp` with `static_assert` on size
- [ ] Effect class in `effects/` with `update()`, `spawn()`, `data()`, `count()`
- [ ] SPIR-V shaders in `shaders/`
- [ ] `GpuParticleBuffer` + pipeline in `ParticleRenderer`
- [ ] Upload method in `ParticleRenderer`
- [ ] Draw call in `drawAll()` at correct blend-order position
- [ ] Release in `ParticleRenderer::quit()`
- [ ] Effect member + spawn method in `ParticleSystem`
- [ ] `update()` call in `ParticleSystem::update()`
- [ ] `upload` call in `ParticleSystem::uploadToGpu()`
- [ ] (Optional) Event struct + handler + dispatcher registration
- [ ] (Optional) Debug count accessor
