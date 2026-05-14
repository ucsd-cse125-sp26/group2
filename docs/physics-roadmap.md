# Physics Engine Roadmap — Toward a Havok-Class System

Production goals: a deterministic, SIMD-accelerated, multithreaded physics engine
sufficient for a competitive multiplayer FPS with Titanfall-style movement,
hand-authored Blender maps, character ragdolls on death, and the headroom to
add dynamic rigid bodies later. This document is the research, gap analysis,
and per-phase implementation plan toward that goal.

Companion docs:
- [docs/physics.md](physics.md) — current implementation reference
- [docs/physics-plan.md](physics-plan.md) — original design plan (LAN, prediction, constants)
- [docs/titanfall-movement-design.md](titanfall-movement-design.md) — movement state machine

---

## Executive Summary

Our current physics is well-architected for **kinematic player movement** against
**static geometry**, with client-side prediction and server reconciliation working
at 128 Hz. The Titanfall-style movement state machine is genuinely sophisticated
and is the part of the game we cannot buy off the shelf.

What we lack vs. a Havok-class engine breaks into three buckets:

1. **Correctness gaps** that cause visible bugs today — most notably the absence of
   **internal-edge filtering / edge welding** on triangle meshes, producing the
   "ghost contact" jitter at floor seams.
2. **Foundational subsystems** required for the features we want next: a real
   **rigid-body dynamics core** (mass, inertia, impulse API), a **sequential-impulse
   constraint solver**, **joints**, **sleeping/islands**, **dynamic broadphase**,
   and **persistent contact manifolds**. Ragdolls require all of these.
3. **Polish and performance work**: **surface materials**, **trigger volumes**,
   **collision events**, **capsule player**, **contact debug visualization**,
   **offline mesh cooking**, **SIMD** in the hot loops, and a **determinism audit**.

The roadmap below is sequenced so each phase ships visible value, preserves the
existing prediction/reconciliation contract, and leaves SIMD-friendly data
layouts in place from the start rather than retrofitting them at the end.

---

## Part I — Reference: Havok-Class Engines

The reference points throughout are Bullet (open source, closely parallels
Havok's architecture), PhysX (NVIDIA), Jolt (Jorrit Rouwé, Horizon Forbidden
West), and the canonical GDC literature: Erin Catto (sequential impulses,
CCD), Dirk Gregorius (SAT, contact manifolds), Gino van den Bergen (GJK),
Erwin Coumans (Bullet internals), Christer Ericson (*Real-Time Collision
Detection*).

### 1.1 Trimesh collision (deep)

#### The internal-edge / ghost-contact problem

A triangle mesh is a **surface**, not a volume. When a convex shape slides
across a flat floor built from many triangles, the contact point can lie
on a shared edge between coplanar triangles. Per-triangle SAT generates a
separating axis from the triangle's edge that perpendicular-projects to the
floor normal — so the chosen MTV points **sideways along the edge** instead
of along the face normal. A capsule rolling across a tiled floor "catches"
every seam. A box on a flat ground accumulates spurious angular velocity.

#### Cook-time detection (the universal first step)

All four production engines build an edge → triangle adjacency map at mesh
cook time and classify each shared edge:

- `cosθ = dot(nA, nB)` of the two adjacent face normals.
- `signed = dot(cross(nA, nB), edgeVec)` — sign separates convex vs. concave.
- **Convex** (`signed > 0`): surface bends outward; contacts here are real.
- **Concave** (`signed < 0`): inward fold (wall-meets-floor); contacts here
  are almost always ghosts.
- **Coplanar / flat** (`cosθ > 1 - ε`, typical ε ≈ 1e-3 → < ~2.5°): no real
  feature, never trust an off-axis normal.

Thresholds in production:

- Bullet (`btGenerateInternalEdgeInfo`): ~0.01 rad (~0.57°) for flat.
- Havok (`hkpMeshShape::WeldingType`): default ~5° tolerance.
- Jolt (`MeshShapeSettings::mActiveEdgeCosThresholdAngle`): default `cos(50°)` —
  an edge is "active" iff convex by ≥50° (i.e., a real corner you should
  feel).

#### Two runtime strategies

**A. Bullet — post-fixup (`btAdjustInternalEdgeContacts`).**
Generate contacts naïvely, then classify the contact's closest feature
(face / edge / vertex) via Voronoi projection and:

- Face → snap contact normal to face normal.
- Convex edge → clamp normal to the cone between the two adjacent face normals.
- Concave / flat edge → snap to the owning face normal or reject.

Data: `btTriangleInfoMap` stores three signed dihedral angles per triangle.

**B. Jolt / PhysX — Voronoi clipping at source.**
Per-triangle "active edge" bits. At runtime, compute the closest feature
on the triangle to the convex; if the closest feature is an **inactive**
edge or a vertex bordered only by inactive edges, **discard the contact**.
The neighbor triangle whose Voronoi region actually owns the contact point
will produce the correct face-normal contact.

Approach B is strictly cleaner — never emits a wrong contact, no normal
heuristic to tune, fewer runtime branches. The trade-off is the cook step
must be 100% correct (consistent winding, complete adjacency).

#### Mesh data structures (typical footprint comparison)

| Engine | Shape | Acceleration | Verts | Per-tri data |
|---|---|---|---|---|
| Bullet | `btBvhTriangleMeshShape` | `btQuantizedBvh` (16-bit AABB, SAH-built) | f32 | optional `btTriangleInfoMap` |
| Havok | `hkpBvCompressedMeshShape` | 4-ary compressed BV-tree (~6–8 B/node) | 16-bit quantized | material idx, welding bits |
| PhysX | `PxTriangleMeshGeometry` | RTree / BV4 | 16- or 32-bit indices, cooked-quantized | material, adjacency, edge flags |
| Jolt | `MeshShape` | implicit BVH in cache-line leaves | f32, 24 B / tri | user data + 3 active-edge bits |

Typical: a 100k-tri level mesh cooks to ~6–10 MB vs ~15–20 MB naïve.

#### Swept / continuous collision against trimeshes

Three production strategies:

- **Linear shape-cast** (cheapest). Sweep the convex along a straight line,
  find first time-of-impact (TOI) per triangle. Bullet:
  `btSubsimplexConvexCast`. Tunnel-free for translation; rotation not
  covered.
- **Conservative advancement (CA)**. Iterate: closest distance `d`, advance
  by `d / vmax` where `vmax` includes angular term `|ω|·r`. Jolt uses CA
  for rotating bodies.
- **Speculative contacts**. Widen each dynamic AABB by `v·dt` in broadphase,
  generate contacts at predicted positions, let the solver clamp motion.
  Box2D / Bullet default; cheap and robust, but allows "ghost forces" from
  contacts that wouldn't have actually happened.

Production engines combine these: speculative for default, CA-based TOI
for `setCcdMotionThreshold` "bullet" bodies. Substepping alone does not
fix tunneling.

### 1.2 Broader featureset (survey)

| Layer | Production approach |
|---|---|
| **Broad phase** | SAP (sweep-and-prune) for mostly-static; dynamic AABB tree (Box2D `b2DynamicTree`, Bullet `btDbvtBroadphase`, Jolt `QuadTree`) for the modern default; spatial hash for uniformly-sized particles. |
| **Narrow phase** | GJK + EPA for convex-convex distance/penetration; SAT for low-face-count polytopes (Gregorius advocates SAT for ≤64-face hulls); MPR as a GJK alternative that handles penetration in one step. |
| **Contact manifold** | After one penetration normal is found, generate up to 4 contact points by clipping incident face against reference face's side planes (Sutherland–Hodgman). Catto's *Box2D Lite* is the canonical reference. |
| **Persistent contacts** | Keep contact points across frames if world position drifts < ~2 cm and depth still satisfies threshold; warm-start solver impulses from last frame's converged values. |
| **Solver** | Sequential Impulses / PGS (Box2D, Bullet) — iterate 4–10 times. Featherstone ABA — O(n) recursive for kinematic chains. PBD / XPBD — Jolt's choice for cloth and soft bodies. |
| **Constraints** | Ball (3 DOF), Hinge (5 DOF), Slider (5 DOF), Cone-Twist (ragdoll shoulder), generic 6-DOF with per-axis lock/limit/free + motor + spring. Motors as velocity or position target; XPBD springs via compliance `α = 1/(k·dt²)`. |
| **CCD** | Per-body modes: discrete (default), linear CCD (translation TOI), full CCD (rotation included). Bullet: `setCcdMotionThreshold`; PhysX: `eENABLE_CCD`. |
| **Character controllers** | Two paradigms: kinematic capsule via shape-cast-and-slide (Havok `hkpCharacterProxy`, Unity, PhysX `PxController`); dynamic constrained capsule (Havok `hkpCharacterRigidBody`). Step-up via height threshold, slope limit ~45–50°. |
| **Ragdolls** | Skeleton of rigid bodies + cone-twist/hinge joints. Maximal coordinates + constraints (cheap, joints can pop); or Featherstone (PhysX `PxArticulationReducedCoordinate`, expensive but stable). |
| **Soft body / cloth / fluids** | Mass-spring or PBD distance constraints (cloth); tetrahedral FEM (PhysX 5 soft body); PBF (Position-Based Fluids, Macklin & Müller 2013). |
| **Queries** | Raycast, shape-cast (sweep convex along segment via Minkowski-sum trick), overlap (returns set), closest-point. All with layer-mask + arbitrary predicate filter. |
| **Triggers / sensors** | Shapes flagged "no-response": generate enter/exit events via manifold cache, do not feed solver. |
| **Sleeping / islands** | Partition contact graph into connected components per step; an island whose every body has `|v| < vSleepThresh` for `tSleepThresh` (typically 0.5 s) is frozen. Wake on contact or applied force. |
| **Multithreading** | Jolt: fork-join job system, every stage parallelized, per-island solver tasks. PhysX: task graph with dependencies + GPU rigid-body pipeline. Bullet: parallel solver. |
| **Determinism** | Required for lockstep multiplayer. Jolt guarantees bit-exact across runs on same binary: fixed iteration order, no cross-thread float reductions, deterministic island assignment, stable body IDs, no `-ffast-math` violations. |
| **Debug viz** | Wireframe colliders, contact points + normals (arrow ∝ impulse), broadphase tree, sleeping state, constraint anchors. Bullet `btIDebugDraw`, PhysX `PxRenderBuffer`. |
| **Mesh cooking** | Offline tool builds: vertex weld map, triangle adjacency, BVH, quantization tables, edge flags, material table, optional V-HACD. Cooked blob is platform/version-stamped; never cooked at runtime in shipping. |

---

## Part II — Current Engine Audit

File:line references are accurate as of branch `upd/trimesh-collision`.

### Simulation core
- **Integration**: semi-implicit Euler. Gravity `Movement.cpp:16-21`, accel `Movement.cpp:39-51`, position update `CollisionSystem.cpp:399-407`.
- **Fixed timestep**: yes, `dt = 1/tickRateHz` (128 Hz default). Server: `ServerGame.cpp:124-186`. Client: `PredictionSystem.hpp:52-53`, `ReconciliationSystem.hpp:100-101`.
- **Substepping**: none beyond the 4-iteration bump loop in `CollisionSystem.cpp:398`.
- **Force / impulse API**: none. Velocity is mutated directly (`ExplosionSystem.cpp:101`, `CollisionSystem.cpp:561`).

### Broad phase
- **Dynamic spatial structure**: none. Player↔world and projectile↔world only.
- **Static trimesh BVH**: per-mesh, leaf size 4 (`TriMeshCollision.cpp:21-105`).
- **Pair caching**: none.

### Narrow phase
- **Pairs**: swept AABB vs `{plane, AABB, brush, cylinder, sphere, trimesh}` (`SweptCollision.hpp:129-159`, `TriMeshCollision.hpp:22`). Sphere cast (`SweptCollision.hpp:182`). Ray vs all + capsule (`Raycast.hpp:44-617`).
- **GJK / MPR**: none.
- **SAT**: 13-axis AABB-vs-triangle in `TriMeshCollision.cpp:144-191` + MTV variant from `:193`.
- **Manifolds**: single-contact `HitResult` (`SweptCollision.hpp:110-115`). Aggregated MTV sum in trimesh depenetration only.
- **Persistent contact cache**: none.

### Solver / constraints
- **Solver**: none. Direct velocity clipping via `clipVelocity` (`Movement.cpp:67-71`) — Quake slide projection.
- **Joints**: none.
- **Friction**: Quake XZ-only ground friction with `stopSpeed` (`Movement.cpp:23-37`). No tangential contact friction.
- **Restitution**: per-projectile `bounceRestitution` (`Projectile.hpp:36`, applied at `CollisionSystem.cpp:561-565`). No general restitution.

### CCD
- **TOI**: swept AABB returns `tFirst` (`CollisionSystem.cpp:400`). Tunnel-free against static.
- **Substepping / rotational CCD**: none.

### Character / movement
- **Controller**: custom kinematic AABB (32×72×32, `CollisionShape.hpp`). `MovementSystem.cpp` (1487 lines) + `CollisionSystem.cpp:359-481`.
- **Step-up**: `tryStepUp` at `CollisionSystem.cpp:307-338` (height 18u, `PhysicsConstants.hpp:51`).
- **Slope / ground**: floor = `normal.y > 0.7` (`CollisionSystem.cpp:411-412`). Snap via downward sweep `:346-357`.
- **State machine**: OnFoot, Sliding, WallRunning, Climbing, LedgeGrabbing (`PlayerStateEnums.hpp`). Wall/climb/ledge via sphere casts in `WallDetection.{hpp,cpp}`.

### Dynamic entities
- **Rigid body component**: none. No mass, inertia tensor, angular velocity, torque.
- **Dynamic entities**: kinematic projectiles only (`CollisionSystem.cpp:483-589`). No rotation.

### Queries
- **Raycast**: full suite (`Raycast.hpp`). World, players, hitboxes.
- **Shape cast**: `sweepAll` (`SweptCollision.hpp:159`), `sphereCast` (`SweptCollision.hpp:182`). No public capsule cast.
- **Overlap / closest-point**: only inside `ExplosionSystem.cpp:63-65`. No general API.
- **Triggers / sensors**: none.

### Performance / runtime
- **Sleeping / islands**: none.
- **Multithreading**: yes — `perf::parallelFor` over entities in `CollisionSystem.cpp:374-480` (TBB-backed).
- **Determinism**: targeted (required for reconciliation; `Parallel.hpp:21-28`). Fixed `dt`, shared TUs. No fixed-point. Not formally audited.
- **SIMD**: none in physics.

### Tooling
- **Debug viz**: collider wireframes (`DebugUI.cpp:1504-1721`). No contact points, normals, or active-edge viz.
- **Authoring**: Blender → GLB → `MapLoader.cpp` (1370 lines, Assimp + V-HACD). Collection-name classification: sphere → cylinder → AABB → brush → V-HACD → trimesh fallback.
- **Cooking**: load-time only. No offline cooker.

### Events / materials
- **Collision callbacks**: none. Inline at `CollisionSystem.cpp:567-588`.
- **Surface material**: enum exists in `Projectile.hpp:13-20`, but every world primitive reports `Concrete` (`Raycast.hpp:489`).

---

## Part III — Gap Analysis

Severity is rated for **this game**: FPS, kinematic player, projectiles,
LAN multiplayer with prediction, hand-authored Blender maps, ragdoll on
death, capsule player desired.

| Area | Status | Severity | Notes |
|---|---|---|---|
| Trimesh edge welding | Missing | **Critical** | Active ghost-contact bug; Voronoi clipping plan in Part IV. |
| Contact manifold (multi-point) | Missing | **Critical** | Required for solver and stable resting on dynamic surfaces. |
| Persistent contact cache | Missing | **Critical** | Solver warm-start; warm-started PGS is 2–3× more stable. |
| Constraint solver (SI / PGS) | Missing | **Critical** | Required for joints → ragdoll → any dynamic rigid stacking. |
| Joints (point, hinge, cone-twist, 6-DOF) | Missing | **Critical** | Cone-twist + hinge needed for ragdoll. |
| Rigid body component (m, I, ω, τ) | Missing | **Critical** | Foundation of dynamic system. Required for ragdoll. |
| `applyForce` / `applyImpulse` API | Missing | **Critical** | Unifies knockback, bounce, explosion; prereq for solver. |
| Coulomb friction at contacts | Partial | **Quality** | Quake ground friction only; tangential needed for resting dynamics. |
| Restitution (per-surface) | Partial | **Quality** | Projectile-only; tie to surface materials. |
| Dynamic broadphase (AABB tree) | Missing | **Critical** | Foundation for any > 50 dynamic bodies. Needed before ragdoll scales. |
| Sleeping / islands | Missing | **Critical** | Ragdoll bodies must sleep when settled (perf). |
| Trigger volumes / sensors | Missing | **Critical** | Required for KOTH zones, pickups, kill volumes, level scripting. |
| Surface material tagging | Partial | **Critical** | Required for footstep audio, decals, impact VFX. |
| Collision callbacks (enter/exit) | Missing | **Critical** | Decouples gameplay from physics internals. |
| Contact debug viz | Missing | **Critical** | Required to debug everything else; cheap. Build first. |
| Capsule player + capsule cast | Missing | **Critical** | User requirement. Removes step/stair edge cases. |
| Rotational CCD | Missing | Quality | Linear sweep covers all current cases. |
| Mesh cooking offline | Partial | **Quality** | Load-time only; should emit deterministic binary. |
| Ragdoll system | Missing | **Critical** | User requirement; depends on rigid bodies + solver + joints + sleeping. |
| Determinism audit | Partial | **Critical** | Reconciliation depends on it; needs formal pass. |
| SIMD in hot loops | Missing | **Critical** | User priority. Bake into data layouts from the start. |
| Cloth / soft body / fluids | Missing | Future | Out of scope for FPS. |

### What we already do well — don't touch
- Titanfall-style movement state machine (`MovementSystem.cpp`).
- Fixed-tick prediction/reconciliation architecture.
- Swept-AABB-vs-primitive Minkowski tests (`SweptCollision.cpp:60-348`).
- Raycast suite (`Raycast.hpp`).
- Per-entity parallel kernel (`CollisionSystem.cpp:374-480`).
- Debug overlay for collider wireframes (`DebugUI.cpp:1504`).

---

## Part IV — Trimesh Collision: Voronoi-Clipping Implementation

Chosen approach: Jolt-style Voronoi clipping at source. Cleaner than Bullet's
post-fixup, no normal-snapping heuristic to tune.

### Mesh data extensions

Add to `WorldTriMesh` (`WorldData.hpp`):

```cpp
struct WorldTriMesh {
    // ... existing fields (vertices, indices, BVH) ...

    // Cooked welding data — one entry per triangle.
    std::vector<glm::vec3> faceNormals;   // unit length, CCW winding
    std::vector<uint8_t>   edgeActive;    // bit i of byte n = edge i active for tri n
    std::vector<uint8_t>   vertActive;    // bit i of byte n = vertex i active for tri n
};
```

Per-triangle data is 7 bytes total (12 B normal + 1 B edge flags + 1 B vert
flags, plus alignment). On a 100k-tri mesh: ~700 KB; rounding for SIMD
alignment: ~1 MB. Acceptable.

### Cook-time pass — `weldTriMesh()` in `MapLoader.cpp`

```cpp
void weldTriMesh(WorldTriMesh& mesh,
                 float coplanarTolerance = glm::radians(2.0f));
```

Algorithm:

1. Compute face normals: for triangle `t` with vertices `v0, v1, v2`,
   `nT = normalize(cross(v1 - v0, v2 - v0))`. Store in `faceNormals[t]`.
2. Build edge → triangle-list map. Key is canonical undirected edge:
   `(min(i, j), max(i, j))` where `i, j` are vertex indices.
3. For each shared edge between triangles A and B:
   - `cosθ = dot(nA, nB)`, `signed = dot(cross(nA, nB), edgeVec)`.
   - Edge is **active** iff `signed > 0` AND
     `cosθ < cos(coplanarTolerance)`. (Convex by more than `tolerance`.)
   - Set the bit in both A's and B's `edgeActive`.
4. Boundary edges (single-triangle) are always active.
5. Vertex `v` is **active** iff at least two of its incident edges are
   active and they span a non-coplanar pair.

### New runtime primitives — `TriMeshCollision.cpp`

Replace `aabbVsTriMTV` (`:203`) and `sweepAABBvsTriangle` (`:286`) with
Voronoi-clipped versions. Core helper:

```cpp
enum class TriRegion { Face, Edge0, Edge1, Edge2, Vert0, Vert1, Vert2 };

// Closest point on triangle to a point, plus region tag.
// Ericson, RTCD §5.1.5.
TriRegion closestPointOnTriangle(
    glm::vec3 p, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2,
    glm::vec3& outClosest);
```

New depenetration:

```cpp
bool depenetrateAABBvsTri(
    glm::vec3 pos, glm::vec3 he,
    glm::vec3 v0, glm::vec3 v1, glm::vec3 v2,
    glm::vec3 faceNormal, uint8_t edgeFlags, uint8_t vertFlags,
    glm::vec3& outMTV)
{
    // 1. Plane test: |signedDist| > radiusOnNormal → no overlap.
    const float r = dot(he, abs(faceNormal));
    const float s = dot(faceNormal, pos - v0);
    if (std::abs(s) > r) return false;

    // 2. Find closest feature on triangle to AABB center.
    glm::vec3 closest;
    const TriRegion region = closestPointOnTriangle(pos, v0, v1, v2, closest);

    // 3. Discard contacts on inactive features (Jolt-style).
    if (region == TriRegion::Edge0 && !(edgeFlags & 1)) return false;
    if (region == TriRegion::Edge1 && !(edgeFlags & 2)) return false;
    if (region == TriRegion::Edge2 && !(edgeFlags & 4)) return false;
    if (region == TriRegion::Vert0 && !(vertFlags & 1)) return false;
    if (region == TriRegion::Vert1 && !(vertFlags & 2)) return false;
    if (region == TriRegion::Vert2 && !(vertFlags & 4)) return false;

    // 4. MTV always along face normal; depth = r - s.
    if (length(pos - closest) > length(he) + epsilon) return false;
    outMTV = faceNormal * (r - s);
    return true;
}
```

Two coplanar triangles sharing an inactive (welded) edge: an AABB straddling
the seam projects with `closest` on the inactive edge of one triangle and on
the **face** of the other. The first returns `false` (discarded), the second
returns the face-normal MTV. The aggregate is one clean `+Y` push — exactly
the behavior we want.

### Integration into `sweepAll` and `depenetrate`

- `SweptCollision.cpp:353` — add trimesh primitive iteration; one inner loop
  over `mesh.triangles`. Keep the per-mesh AABB quick-reject. Drop the BVH
  if linear is fast enough; keep it behind a flag if you prefer.
- `CollisionSystem.cpp:280` — replace `depenetrateAABBvsTriMesh` call with a
  per-triangle loop; sum MTVs and apply once. **Drop the 4-pass loop**; with
  Voronoi clipping it's not needed.

### SIMD considerations from day one
- `faceNormals` as SoA: `vector<float> nx, ny, nz` rather than AoS `vec3`.
  Enables 4-wide (SSE) or 8-wide (AVX) signed-distance computation.
- `edgeActive` / `vertActive` packed for fast mask loading.
- BVH leaf format aligned to cache line: 64 B = exactly four triangles
  worth of pointer + AABB metadata.

### Validation
- Diagonal seam on a 2-triangle floor → no snag.
- Subdivided 100-tri floor → feels like one solid surface.
- 90° wall-meets-floor corner → still depenetrates (this is the convex-edge
  case; edge stays active).
- 30° ramp → correct ramp normal.
- Stair step (two non-coplanar triangles meeting at convex edge) →
  depenetrates correctly.

---

## Part V — Holistic Roadmap

### Sequencing rationale

1. **Tooling first.** Phase 1 (debug viz) makes every subsequent phase
   diagnosable. Skipping it doubles debugging time on phases 2 and 13.
2. **Trimesh welding next.** The currently visible bug.
3. **Standalone game-unblockers in parallel.** Materials, triggers, capsule
   — independent of each other; can be worked in any order.
4. **Foundations for dynamics.** Impulse API → rigid bodies → broadphase →
   manifolds → solver → joints → sleeping. Each strict prereq for the next.
5. **Ragdoll lands once foundations are in.**
6. **Polish phases.** Offline cooking, SIMD pass, determinism audit. These
   touch hot code and benefit from being last.

### Dependency graph

```
Phase 1 ─ Debug viz ──┬→ Phase 2 ─ Trimesh welding ──┐
                      │                              ├→ Phase 14 ─ Offline cooking
                      └→ all later phases (helpful)  │
                                                     │
Phase 3 ─ Surface materials ─────────────────────────┤
Phase 4 ─ Triggers + events ─────────────────────────┤
                                                     │
Phase 5 ─ Capsule player (needs P1) ─────────────────┤
                                                     │
Phase 6 ─ Impulse API ──→ Phase 7 ─ Rigid bodies ──┐ │
                                                   ├─┤
Phase 8 ─ Dynamic broadphase ←─────────────────────┤ │
Phase 9 ─ Contact manifolds + persistence ←────────┤ │
                                                   ↓ │
                          Phase 10 ─ Solver (PGS) ←┘ │
                                  │                  │
                       ┌──────────┼──────────┐       │
                       ↓          ↓          ↓       │
              Phase 11 Joints  Phase 12 Sleep  Phase 13 Ragdoll
                                              │
                                              └→ depends on 7,10,11,12

Phase 15 ─ Determinism audit + SIMD pass (continuous, big sweep at end)
```

Parallelizable work streams:
- **Stream A** (collision correctness): P1 → P2 → P14
- **Stream B** (gameplay polish): P3, P4 in any order
- **Stream C** (player feel): P1 → P5
- **Stream D** (dynamics core): P6 → P7 → P8/P9 → P10 → P11/P12 → P13

### Phase list

| # | Phase | Severity | Stream |
|---|---|---|---|
| 1 | Contact / normal / edge debug visualization | Critical | A |
| 2 | Trimesh edge welding (Voronoi clipping) | Critical | A |
| 3 | Surface material tagging | Critical | B |
| 4 | Trigger volumes & collision events | Critical | B |
| 5 | Capsule player + capsule shape cast | Critical | C |
| 6 | Unified force / impulse / torque API | Critical | D |
| 7 | Rigid body component (mass, inertia, ω, τ) | Critical | D |
| 8 | Dynamic broadphase (dynamic AABB tree) | Critical | D |
| 9 | Contact manifolds + persistent contacts | Critical | D |
| 10 | Constraint solver (sequential impulse / PGS) | Critical | D |
| 11 | Joints: point, hinge, cone-twist, 6-DOF | Critical | D |
| 12 | Sleeping + island management | Critical | D |
| 13 | Ragdoll system | Critical | D |
| 14 | Offline mesh cooking pipeline | Quality | A |
| 15 | Determinism audit + SIMD pass | Critical | continuous |

---

## Part VI — Per-Phase Implementation Plans

Each phase plan: **goal**, **deps**, **files**, **data**, **algorithm**,
**SIMD/perf notes**, **determinism notes**, **validation**, **rough effort**.
Efforts assume one engineer familiar with the codebase, working full-time.

---

### Phase 1 — Contact / normal / edge debug visualization

**Goal.** Render contact points (small spheres), contact normals (arrows
scaled by impulse magnitude), and triangle-mesh active/inactive edges, all
toggleable via `DebugUI`. Required to validate every subsequent phase.

**Deps.** None.

**Files.**
- `src/client/debug/DebugUI.cpp:1504` — add new toggle groups: "Contacts",
  "Mesh edges".
- New `src/ecs/physics/DebugCollisionDraw.{hpp,cpp}` — accumulator that
  collision code feeds; debug renderer drains it each frame.
- `src/ecs/systems/CollisionSystem.cpp` — call accumulator when a contact
  is produced.

**Data.**
```cpp
struct DebugContact {
    glm::vec3 point;
    glm::vec3 normal;
    float impulseMag;   // arrow length proxy
    uint32_t triangleId;// -1 if not on a trimesh
};

// Per-tick buffer drained on frame end.
struct DebugCollisionFrame {
    std::vector<DebugContact> contacts;
};
```

**Algorithm.** Existing collision code calls `pushContact(...)` instead of
just updating velocity. Debug renderer reads in `DebugUI`; no game-side
visibility into this buffer.

**SIMD / perf.** Disabled by default; cost = zero when off. Drain via
double-buffer to avoid per-tick allocation.

**Determinism.** Buffer is render-only; never affects sim state.

**Validation.**
- Walk along a wall — arrows perpendicular to wall, length stable.
- Walk diagonally on a 2-triangle floor — see the ghost-contact arrows
  pointing along the seam (this is the bug we're about to fix).
- Toggle mesh-edge viz: active edges drawn green, inactive (welded) drawn red.

**Effort.** 1–2 days.

---

### Phase 2 — Trimesh edge welding (Voronoi clipping)

**Goal.** Replace per-triangle SAT MTV with face-normal MTV gated by
Voronoi-region active-edge filtering. Floor seams stop snagging.

**Deps.** Phase 1 (for validation).

**Files.**
- `src/ecs/physics/WorldData.hpp` — extend `WorldTriMesh`.
- `src/ecs/physics/MapLoader.cpp` — add `weldTriMesh()`, call after BVH build.
- `src/ecs/physics/TriMeshCollision.cpp` — replace `aabbVsTriMTV`
  (`:203`) and `sweepAABBvsTriangle` (`:286`).
- `src/ecs/physics/TriMeshCollision.hpp` — update API.
- `src/ecs/systems/CollisionSystem.cpp:280` — drop 4-pass loop, single
  aggregated pass.

**Data.** See Part IV. Add `faceNormals`, `edgeActive`, `vertActive` to
`WorldTriMesh`.

**Algorithm.** See Part IV.

**SIMD / perf.**
- Face normals as SoA (`vector<float> nx, ny, nz`) for 4/8-wide signed
  distance computation across multiple triangles.
- Active-edge flag byte allows mask-loading for branchless region
  rejection on x86 (`_mm256_movemask_epi8`).
- Linear iteration: target ≤ 5000 triangles per map.
- If a map needs more, keep the BVH (cheap to do — just don't drop it).

**Determinism.**
- Cook step uses canonical edge ordering (sorted index pair) → mesh-build
  output is deterministic.
- Runtime aggregation: sum MTVs in **triangle-index order**. No reductions
  across threads.

**Validation.** See Part IV validation list. Then run all multiplayer
prediction tests — there should be zero divergence between client and server
sim on welded meshes.

**Effort.** 3–5 days.

---

### Phase 3 — Surface material tagging

**Goal.** Every primitive (and every triangle) carries a `SurfaceType`.
Raycasts, hits, and contacts return it. Drives footstep SFX, decals,
impact VFX, and per-surface restitution.

**Deps.** None.

**Files.**
- `src/ecs/physics/WorldData.hpp` — add `surfaceType` field to every
  primitive struct; `vector<uint8_t> triangleMaterials` to `WorldTriMesh`.
- `src/ecs/physics/MapLoader.cpp` — read from Blender material name (e.g.,
  `mat_metal`, `mat_concrete`).
- `src/ecs/physics/SweptCollision.hpp:110` — add `uint8_t surfaceType` to
  `HitResult`.
- `src/ecs/physics/Raycast.hpp:489` — replace hardcoded `Concrete`.
- `src/game/Projectile.hpp:13` — enum is already there; just plumb it.

**Data.** One byte per primitive; one byte per triangle. Negligible memory.

**Algorithm.** Blender material name prefix `mat_` is parsed in `MapLoader`
into a `SurfaceType` enum value at load. Per-triangle material indices come
from Assimp's material-per-face data.

**SIMD / perf.** Material lookup is one byte access on hit — no perf concern.

**Determinism.** Static data — no runtime variability.

**Validation.**
- Map with explicit metal/wood/concrete sections → raycast reports correct
  type at every probe point.
- Footstep system already keys off the enum (`WeaponSystem.cpp:621`); should
  now produce varied sounds.

**Effort.** 2 days.

---

### Phase 4 — Trigger volumes & collision events

**Goal.** A shape flagged "trigger" generates `enter` / `stay` / `exit`
events instead of physical response. Gameplay code (KOTH zones, pickups,
kill volumes) subscribes without touching `CollisionSystem.cpp`.

**Deps.** None. Best done before adding constraint solver (so trigger
filter is part of the unified pipeline).

**Files.**
- New `src/ecs/physics/Trigger.hpp` — component flag.
- New `src/ecs/physics/CollisionEvents.hpp` — event types.
- `src/ecs/systems/CollisionSystem.cpp` — generate events at the same
  point we currently inline projectile-hit handling (`:567-588`).
- Subscribers wire up via existing ECS event bus or new dispatcher.

**Data.**
```cpp
struct TriggerVolume { uint32_t layerMask; bool fireOnPredictedClient; };

struct CollisionEvent {
    enum Type { Enter, Stay, Exit };
    entt::entity a, b;
    Type type;
    glm::vec3 point, normal;
    uint8_t surfaceA, surfaceB;
};
```

**Algorithm.** Each tick after collision resolution, diff this tick's
overlap set against last tick's; emit Enter for new pairs, Exit for missing.

**SIMD / perf.** Overlap set per entity in a small hash; diff is O(n) per
entity per tick.

**Determinism.** Event order = entity-id-sorted pair iteration. Critical:
client prediction must not fire one-shot events (e.g., kill volumes)
locally — gate with `fireOnPredictedClient`. Events on the server are
authoritative.

**Validation.** Walk into a test trigger → console logs `Enter`. Stay
inside → repeated `Stay`. Leave → `Exit`. Across network: server fires,
client mirrors via state replication.

**Effort.** 2–3 days.

---

### Phase 5 — Capsule player + capsule shape cast

**Goal.** Replace player AABB with capsule. Add capsule shape cast as a
first-class query. Smoother feel on stairs, ramps, and round geometry.

**Deps.** Phase 1 (for validation), Phase 2 ideally (welded trimeshes).

**Files.**
- `src/ecs/components/CollisionShape.hpp` — add capsule variant
  (height, radius).
- `src/ecs/physics/SweptCollision.{hpp,cpp}` — new `sweepCapsuleVs*`
  functions for each primitive.
- `src/ecs/physics/TriMeshCollision.cpp` — capsule-vs-triangle (uses
  the same face-normal-with-Voronoi logic, but the "Minkowski radius" is
  capsule radius + projection of capsule axis on normal).
- `src/ecs/systems/MovementSystem.cpp` — use new shape for step-up,
  ground-snap, wall-detect.
- `src/ecs/systems/CollisionSystem.cpp` — dispatch by shape type.

**Data.**
```cpp
struct CapsuleShape {
    float radius;      // typically 16 u
    float halfHeight;  // axis length / 2, excluding caps; typically 32 u
    // Axis is implicitly +Y in local space (vertical capsule).
};
```

**Algorithm.** Per primitive: closest-point-on-segment-to-primitive +
swept-sphere logic.
- **Capsule vs plane** — segment-vs-plane sweep, simple.
- **Capsule vs box** — Minkowski sum of capsule and box; reduces to swept-
  sphere vs rounded box (Ericson §5.5.7).
- **Capsule vs cylinder** — 2D circle sweep in XZ + 1D slab in Y on the
  segment.
- **Capsule vs sphere** — segment-segment closest-point + radius sum.
- **Capsule vs triangle** — segment-vs-plane sweep, plus Voronoi region
  check at contact point, plus radius bookkeeping.

**SIMD / perf.** Capsule-vs-many-triangles is the hot path; SoA layout
from Phase 2 already supports it.

**Determinism.** Same iteration order as AABB sweep; no float-reduction
changes.

**Validation.**
- Climb a flight of stairs without juddering (the AABB version was OK
  but the corners are a known issue).
- Walk into a cylinder — round-on-round feels right.
- Wallrun + ledgegrab still work (state machine integration). Check
  carefully: the existing wall/ledge detection uses sphere casts already,
  but step-up uses AABB-vs-floor; switching to capsule may change the step
  apex by a few units.

**Effort.** 5–7 days. Risk: tuning movement constants to feel identical
after the shape change.

---

### Phase 6 — Unified force / impulse / torque API

**Goal.** A single API to apply forces and impulses to entities. Replaces
direct velocity mutation in knockback, projectile bounce, explosion.
Prereq for solver (which produces impulses).

**Deps.** None as written, but most useful with Phase 7.

**Files.**
- New `src/ecs/physics/Forces.hpp` — API.
- New `src/ecs/components/RigidBody.hpp` — even before full rigid bodies,
  introduce a placeholder component holding `vec3 forceAccum`,
  `vec3 impulseAccum`. (Phase 7 will expand it.)
- `src/ecs/systems/ExplosionSystem.cpp:101` — replace direct `vel` write
  with `applyImpulse(entity, dir * mag)`.
- `src/ecs/systems/CollisionSystem.cpp:561-565` — same.
- `src/ecs/systems/MovementSystem.cpp:1233` — integrate accumulated forces
  and impulses into velocity before movement state machine runs.

**Data.**
```cpp
namespace forces {
    void applyForce  (entt::registry&, entt::entity, glm::vec3 worldForce);
    void applyImpulse(entt::registry&, entt::entity, glm::vec3 worldImpulse);
    void applyForceAtPoint  (entt::registry&, entt::entity, glm::vec3 F, glm::vec3 worldPoint);
    void applyImpulseAtPoint(entt::registry&, entt::entity, glm::vec3 J, glm::vec3 worldPoint);
    void applyTorque (entt::registry&, entt::entity, glm::vec3 worldTorque);
}
```

**Algorithm.** `applyForce` adds to accumulator. Tick start: integrate
accumulators into velocity / angular velocity. Tick end: clear.

**SIMD / perf.** Accumulator update is `vel += impulse * invMass` — easily
SIMD-vectorized across entities.

**Determinism.** Application order must be stable; sort by entity ID
before integrating.

**Validation.**
- Old behavior preserved bit-for-bit (no solver yet — same `vel = ...`
  outcome).
- Rocket explosion still throws player same distance; grenade bounce same
  rebound angle.

**Effort.** 2 days.

---

### Phase 7 — Rigid body component (mass, inertia, ω, τ)

**Goal.** Real rigid-body component. Enables linear + angular dynamics.
Static entities have infinite mass / zero inverse-mass.

**Deps.** Phase 6.

**Files.**
- `src/ecs/components/RigidBody.hpp` — expand.
- New `src/ecs/physics/Inertia.hpp` — analytical inertia tensors for box,
  capsule, sphere; numerical for arbitrary shapes.
- `src/ecs/components/Transform.hpp` — add `glm::quat orientation`,
  `glm::vec3 angularVelocity`.
- `src/ecs/systems/MovementSystem.cpp` — integrate orientation
  (`q += 0.5 * vec4(ω, 0) * q * dt`; renormalize).

**Data.**
```cpp
struct RigidBody {
    float invMass;            // 0 = static / kinematic
    glm::mat3 invInertia;     // world-space; recomputed each tick from local
    glm::mat3 localInvInertia;// constant; cooked from shape
    glm::vec3 forceAccum;
    glm::vec3 torqueAccum;
    glm::vec3 impulseAccum;
    glm::vec3 angImpulseAccum;
    float linearDamping  = 0.0f;
    float angularDamping = 0.05f;
};
```

**Algorithm.** Semi-implicit Euler for angular:
```
ω += invInertia * τ * dt
q += 0.5 * vec4(ω, 0) * q * dt
q = normalize(q)
invInertiaWorld = R * localInvInertia * Rᵀ   // R = mat3(q)
```

**SIMD / perf.** Per-entity rigid-body update is a small constant cost.
Layout `RigidBody` to be cache-line-aligned (64 B). For ragdoll's 15
bodies per character, hot data fits in one or two cache lines.

**Determinism.** Quaternion normalization after every integration step.
`-fno-fast-math` for the integration code path (assert in CI).

**Validation.**
- Drop a freely-falling cube from rest → no rotation, lands correctly.
- Apply torque pulse → spins; angular velocity decays at expected rate.
- Apply impulse off-center → both linear and angular response, with
  correct ratios per Newton.

**Effort.** 3–4 days.

---

### Phase 8 — Dynamic broadphase (dynamic AABB tree)

**Goal.** O(log n) overlap queries for dynamic bodies. Box2D-style
`b2DynamicTree`.

**Deps.** Phase 7.

**Files.**
- New `src/ecs/physics/Broadphase.hpp`/`.cpp` — dynamic AABB tree.
- `src/ecs/systems/CollisionSystem.cpp` — query tree instead of iterating
  all entities.

**Data.** Standard left/right AABB tree with parent pointers, "fat AABB"
padding (5% expansion to amortize re-insertion).

**Algorithm.** Insert/update via tree descent with surface-area heuristic.
Query: stack-based traversal with `AABB.overlap` cull.

**SIMD / perf.**
- Pack AABBs as SoA: 6 floats per body, contiguous arrays for `minX[]`,
  `minY[]`, ..., `maxZ[]`. AABB overlap is then 4 or 8 bodies at once
  via packed comparison + bitwise AND.
- Tree node format: 32 B (parent, child0, child1, height, AABB) — 2 per
  cache line.

**Determinism.** Tree topology depends on insertion order: insert by
sorted entity ID.

**Validation.**
- Insert 1000 random AABBs; query an arbitrary AABB; result matches brute
  force.
- Move bodies and re-update; query results stay correct.
- Profile: 1000 dynamic bodies × 60 ticks/s with the tree should run in
  < 1 ms.

**Effort.** 3–4 days.

---

### Phase 9 — Contact manifolds + persistent contacts

**Goal.** Multi-point contact manifolds (up to 4 points per pair).
Persistent across frames for warm-start.

**Deps.** Phase 7, Phase 8.

**Files.**
- New `src/ecs/physics/ContactManifold.hpp`/`.cpp`.
- New `src/ecs/physics/ContactCache.hpp`/`.cpp` — pair → manifold map.
- `src/ecs/physics/SweptCollision.cpp` — emit manifolds instead of
  single `HitResult` (keep `HitResult` for raycast queries).

**Data.**
```cpp
struct ContactPoint {
    glm::vec3 worldPositionA;  // body A frame, in world coords
    glm::vec3 worldPositionB;
    glm::vec3 localA;          // body A local space (for warm-start)
    glm::vec3 localB;
    float depth;
    float normalImpulse = 0;   // accumulated this tick (PGS)
    float tangentImpulse[2] = {0, 0};
    uint32_t featureA, featureB; // for caching
};

struct ContactManifold {
    glm::vec3 normal;          // points from A → B
    int pointCount;
    ContactPoint points[4];
    entt::entity a, b;
};
```

**Algorithm.** For each colliding pair, generate up to 4 points by
Sutherland-Hodgman clipping of incident face against reference face's
side planes (Catto, *Box2D Lite*). Per-frame: match new points to cached
ones by feature ID; if found, copy accumulated impulses for warm-start.

**SIMD / perf.** Manifold storage in SoA per island (Phase 12). Per-pair
clipping: branchy, runs at most O(pairs) per tick.

**Determinism.** Pair iteration sorted by (entityA, entityB) ID. Feature
IDs derived from shape + face index.

**Validation.**
- Stack two boxes — bottom box has 4 contact points with top box, all
  with depth ≈ 0 after solve, no jitter.
- Drag a third box across the top — manifold tracks correctly,
  warm-start prevents oscillation.

**Effort.** 5–7 days.

---

### Phase 10 — Constraint solver (sequential impulse / PGS)

**Goal.** Solve contact + friction + future joint constraints via 8–10
iterations of sequential impulses.

**Deps.** Phase 7, 8, 9.

**Files.**
- New `src/ecs/physics/Solver.{hpp,cpp}`.
- New `src/ecs/physics/Constraint.hpp` — base constraint interface.
- `src/ecs/systems/CollisionSystem.cpp` — replace direct velocity clip
  with solver invocation.

**Data.**
```cpp
struct ContactConstraint {
    int manifoldIndex;
    float effectiveMassN[4];     // normal direction
    float effectiveMassT[4][2];  // tangents
    float bias[4];               // Baumgarte / restitution
    float friction;              // Coulomb mu
    float restitution;
};
```

**Algorithm.** Catto sequential impulses (GDC 2005). Per iteration, per
contact:
1. Compute relative velocity at contact point.
2. Compute normal impulse `λ = -(vRel · n + bias) / effectiveMass`.
3. Clamp accumulated `λ ≥ 0`.
4. Apply to both bodies.
5. Tangent impulses with Coulomb friction clamp `|λ_T| ≤ μ * λ_N`.

8–10 iterations typically sufficient for visual quality.

**SIMD / perf.** This is the hottest loop in the engine.
- All effective-mass and impulse data in SoA per island.
- Process 4 or 8 contacts per SIMD lane.
- The Gauss-Seidel dependency is what makes this hard to fully vectorize;
  the standard technique is **graph coloring** to find batches of
  independent constraints that can be solved in parallel within an
  iteration. Jolt does this.

**Determinism.** Iteration order is the determinism contract. Constraints
ordered by (bodyA-id, bodyB-id). No cross-thread reductions inside an
island.

**Validation.**
- Stack of 5 boxes settles in < 50 ticks, no jitter, no penetration > 1 mm.
- Box on a wedge slides at the right rate per friction coefficient.
- Test with restitution = 1 → bounce conserves energy within 5%.

**Effort.** 10–14 days. This is the single biggest phase.

---

### Phase 11 — Joints

**Goal.** Point (ball), hinge, cone-twist, and 6-DOF generic joints. All
solver-driven.

**Deps.** Phase 10.

**Files.**
- New `src/ecs/physics/Joints.hpp`/`.cpp`.
- New components per joint type.

**Data.** Each joint type derives from `Constraint`. Standard formulations
in Catto, Bullet's `btTypedConstraint`, Jolt's `Constraint.h`.

**Algorithm.** Each joint is a set of rows in the solver. A hinge: 3 linear
+ 2 angular = 5 constraint rows. Cone-twist: 3 linear + 2 swing-cone + 1
twist-limit = 6 rows when active.

**SIMD / perf.** Joint constraint rows go through the same SoA solver as
contacts. Negligible per-joint cost in iteration.

**Determinism.** Standard constraint ordering by joint ID.

**Validation.**
- Pendulum (point joint + gravity) swings with correct period for arm
  length.
- Hinge door rotates freely about axis only.
- Cone-twist shoulder respects swing and twist limits — body cannot turn
  head past 90°.

**Effort.** 4–6 days.

---

### Phase 12 — Sleeping + island management

**Goal.** Bodies and constraint islands at rest skip simulation. Critical
for ragdoll perf (15+ bodies per dead character).

**Deps.** Phase 10.

**Files.**
- `src/ecs/physics/Solver.cpp` — island partitioning before solve.
- New `src/ecs/physics/Sleep.{hpp,cpp}`.

**Data.** Per-body `sleepTimer`, `isAsleep`. Per-island `allAsleep`.

**Algorithm.** Union-find over contact pairs + joints each tick →
connected components = islands. A body sleeps when `|v| < vThresh` and
`|ω| < ωThresh` for > 0.5 s. An island sleeps only if all members sleep.
Wake on contact, applied force, or joint other end waking.

**SIMD / perf.** Sleeping bodies skipped entirely from integration and
solve. Empty islands processed in O(1).

**Determinism.** Union-find with rank uses entity IDs; output island IDs
deterministic.

**Validation.**
- Ragdoll falls, settles, sleeps within 1 s. CPU profile shows zero
  cost for it after.
- Kick a sleeping body → wakes immediately.

**Effort.** 3 days.

---

### Phase 13 — Ragdoll system

**Goal.** On character death, switch from animated skeleton to physics-
driven ragdoll. Mesh skinning follows the simulated bones.

**Deps.** Phase 7, 10, 11, 12. Also requires a skinned-mesh renderer; if
absent, that's a prerequisite from the rendering side (verify in
`src/client/render/`).

**Files.**
- New `src/ecs/physics/Ragdoll.{hpp,cpp}`.
- New `src/ecs/components/RagdollComponent.hpp`.
- `src/ecs/systems/CharacterDeathSystem.{hpp,cpp}` — handle the
  transition.

**Data.** Per character: ~15 rigid bodies (skull, neck, torso, pelvis,
upper/lower arms ×2, hands ×2, upper/lower legs ×2, feet ×2). Joints:
- Neck → torso: cone-twist (limited swing 40°, twist 60°).
- Torso ↔ pelvis: cone-twist (small range).
- Shoulders: cone-twist (wide swing, full twist).
- Elbows / knees: hinge (limited range, one DOF).
- Hips: cone-twist.
- Wrists / ankles: cone-twist (small).

**Algorithm.** Rigid body shapes are capsules sized from the character's
bind-pose bone lengths. At death:
1. Sample current animated bone transforms.
2. Initialize ragdoll body transforms + velocities (inherit linear
   velocity from prior character velocity; angular from animation
   velocity if available).
3. Disable normal movement system; enable ragdoll system.
4. Each tick: dynamics simulate; mesh skin reads body transforms.
5. After settle (Phase 12 sleep): freeze + optionally fade out.

**SIMD / perf.** All bodies in one island → one solver batch. 15 bodies
× ~24 contacts at peak. Should be < 0.1 ms per ragdoll after Phase 15
SIMD pass.

**Determinism.** Critical for replay/spectate. Body IDs and joint order
seeded from character entity ID.

**Validation.**
- Shoot a character; corpse falls naturally over a railing.
- Multiple corpses don't interpenetrate; they pile.
- After 5 s, all bodies asleep, zero CPU cost.

**Effort.** 7–10 days.

---

### Phase 14 — Offline mesh cooking pipeline

**Goal.** A separate `cook` tool ingests Blender GLB, emits a versioned
binary blob containing vertices, indices, BVH, face normals, edge flags,
material IDs. Runtime loads in O(file-size) with zero per-load work.

**Deps.** Phase 2 (welded mesh data is the main thing being cooked).

**Files.**
- New `tools/cooker/main.cpp` — standalone executable.
- `src/ecs/physics/MapLoader.cpp` — branch on `.cookedmap` vs `.glb`.
- New `src/ecs/physics/CookedMapFormat.hpp` — binary layout, version stamp.

**Data.** Binary format with platform-specific endianness + alignment.
Version int at top; mismatched version → fatal load error.

**Algorithm.** Cooker runs at build time (or content edit time): load
GLB → run all the existing load-time processing → write blob. Runtime:
`mmap` blob, fix up offsets if needed.

**SIMD / perf.** Load time goes from O(seconds) to O(milliseconds).

**Determinism.** Same input GLB always produces bit-identical blob (modulo
the cooker version). Cooker itself must be deterministic — no parallel
ordering issues.

**Validation.** Round-trip: cook a map, load it, compare every primitive
to the load-time-cooked version. Zero divergence.

**Effort.** 3–4 days.

---

### Phase 15 — Determinism audit + SIMD pass

**Goal.** Formal audit and explicit SIMD intrinsics in the hottest loops.

**Deps.** All prior phases functionally complete.

**Files.** Audit-driven; expect changes in:
- `SweptCollision.cpp`, `TriMeshCollision.cpp`, `Solver.cpp`,
  `Broadphase.cpp`, integration in `MovementSystem.cpp`.

**Audit checklist.**
- [ ] No `-ffast-math` anywhere in the physics translation units.
- [ ] All `std::accumulate` / `std::reduce` calls on float data are either
  ordered or replaced with explicit summation in entity-ID order.
- [ ] No thread-local state with implementation-defined ordering.
- [ ] All `parallelFor` kernels are pure — output indexed by entity slot,
  no shared accumulators.
- [ ] Quaternion renormalization after every integration step.
- [ ] Body iteration in solver is sorted by stable ID.
- [ ] Tree topology in broadphase reproducible across client/server.
- [ ] CI test: run sim for 1000 ticks with fixed input; hash final
  state; check bit-equality across two runs and across client/server.

**SIMD pass.** Targets in priority order:
1. AABB-tree overlap query (Phase 8) — 8-wide AVX.
2. PGS solver inner loop (Phase 10) — 4-wide SSE with graph coloring.
3. Trimesh per-triangle face-normal distance test (Phase 2) — 8-wide.
4. Integration step (Phase 7) — across entities, 4-wide.
5. Manifold clipping (Phase 9) — limited gains; defer.

Use `glm::simd*` types where available; drop to intrinsics
(`<immintrin.h>`) for the inner kernels. Wrap behind feature detection:
runtime CPUID check + dispatch table; SSE2 baseline + AVX2 fast path.

**Determinism + SIMD interaction.** Horizontal reductions (`hadd_ps`) can
differ across SIMD vector widths if the lane count differs between
client and server builds. Fix: always reduce in a fixed order (e.g.,
lane 0 → lane N-1), regardless of intrinsic shortcut.

**Validation.** CI bit-equality test (above). Profile each phase: target
50% reduction in physics CPU time at 64 active dynamic bodies and 5
ragdolls.

**Effort.** 7–10 days.

---

## Total scope

| Stream | Phases | Sequential effort |
|---|---|---|
| A (collision correctness) | 1, 2, 14 | ~10 days |
| B (gameplay polish) | 3, 4 | ~5 days |
| C (player feel) | 5 | ~7 days |
| D (dynamics + ragdoll) | 6, 7, 8, 9, 10, 11, 12, 13 | ~40 days |
| Continuous | 15 | ~10 days |

Single engineer, ~70 days end-to-end. Two streams in parallel (A+B/C and D
overlap) brings it to ~40 calendar days. Practical recommendation: ship
phases 1–5 first (visible improvements, all standalone), then commit to
the D stream as a single ~6-week push.

---

## Appendices

### A. References

- Christer Ericson, *Real-Time Collision Detection* (Morgan Kaufmann, 2004) — closest-point algorithms, Voronoi regions, SAT.
- Erin Catto, GDC 2005 *Iterative Dynamics* — sequential impulses.
- Erin Catto, GDC 2014 *Continuous Collision* — speculative contacts, CCD.
- Dirk Gregorius, GDC 2013/2014/2015 — SAT, contact manifolds, robust hull-hull.
- Gino van den Bergen, *Collision Detection in Interactive 3D Environments* — GJK, EPA.
- Erwin Coumans, Bullet source — `btInternalEdgeUtility.cpp`, `btGjkPairDetector.cpp`, `btSequentialImpulseConstraintSolver.cpp`.
- Jorrit Rouwé, Jolt source — `MeshShape.cpp`, `ContactConstraintManager.cpp`, GDC 2022 *Architecture Overview*.
- Müller et al., *Position Based Dynamics* (2006); Macklin et al., *XPBD* (2016).
- Pierre Terdiman, PhysX dev blog — "the great trimesh problem".

### B. Glossary

- **MTV** — Minimum Translation Vector. The shortest displacement that
  separates two overlapping shapes.
- **SAT** — Separating Axis Theorem. Two convex shapes are disjoint iff
  there exists an axis on which their projections do not overlap.
- **GJK** — Gilbert-Johnson-Keerthi. Iterative convex-vs-convex distance
  algorithm; produces simplex.
- **EPA** — Expanding Polytope Algorithm. Run after GJK detects
  penetration to find penetration depth + normal.
- **PGS** — Projected Gauss-Seidel. Iterative constraint solver; each
  pass updates one constraint at a time using current velocities.
- **CCD** — Continuous Collision Detection. Avoids tunneling at high
  speed.
- **TOI** — Time of Impact. The first `t ∈ [0, 1]` where a swept body
  first touches an obstacle.
- **BVH** — Bounding Volume Hierarchy. Tree of nested AABBs (or other
  bounds) for spatial queries.
- **SAP** — Sweep and Prune. Broadphase via sorted endpoint arrays per
  axis.
- **Voronoi region** — Subdivision of space by closest feature of a
  shape (face, edge, vertex).
- **Welded edge** — A triangle-mesh edge shared by two faces of similar
  normal; marked as "internal" / "inactive" so it doesn't generate
  contact normals.
- **Warm start** — Reusing the previous frame's converged impulses as
  the initial guess this frame. Speeds PGS convergence ~2–3×.
- **XPBD** — Extended Position Based Dynamics; PBD with physically
  meaningful stiffness via compliance `α = 1/(k·dt²)`.
- **SoA / AoS** — Structure-of-arrays / Array-of-structures. SoA enables
  SIMD across elements.
