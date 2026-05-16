# Physics Future Path — Capsule Collision and Wallrun Rebuild

This document is the next-stage plan for the physics layer: completing the
capsule transition (currently tagged but unused on the sweep path), adopting
modern swept-collision techniques, and rebuilding wallrunning ground-up around
a wall-attached kinematic frame instead of "modify velocity, hope the bump loop
catches up."

It is a continuation of `physics-roadmap.md`, not a replacement. The 15 phases
of that roadmap are delivered as structural scaffolding; this document
addresses the gaps between *scaffolded* and *load-bearing*.

## Honest State of Play

| Subsystem | Status | Reality check |
|---|---|---|
| Trimesh BVH | Working | Flat array, SAH-like centroid partition, 4-tri leaves. `TriMeshCollision.cpp:484`. |
| Edge welding (Voronoi) | Working on depenetration | Dihedral classification, `edgeActive` / `vertActive` bitfields. `TriMeshCollision.cpp:354-371`. |
| Edge welding on sweep | Deliberately **disabled** | `TriMeshCollision.cpp:452`: `(void)edgeFlags; (void)vertFlags;` — both coplanar triangles report identical hits; bump loop's velocity clip resolves. Correct for AABB; wrong for capsule. |
| Player capsule **type tag** | Set | `ServerGame.cpp:694` — `.type = Capsule, .halfHeight = 20.0f, radius = 16.0f`. |
| Player capsule **sweep / depenetrate** | **Not implemented** | All swept queries (`sweepAABB`, `sweepAABBvsBox`, `sweepAABBvsBrush`, `sweepAABBvsCylinder`, `sweepAABBvsSphere`, `sweepAABBvsTriMesh`) consume `halfExtents` as AABB. The comment at `ServerGame.cpp:688-691` admits as much: "the capsule fields drive Phase-5+ shape-aware paths." |
| Player movement collision | AABB (of the capsule's bounding box) | The standoff math in `MovementSystem.cpp:786-792` computes `|n.x|·hx + |n.z|·hz` — the *AABB* Minkowski envelope, not the capsule one. |
| Hitscan capsules | Working | Per-bone capsules for damage queries; separate from environment collision. |

The result: `physics-roadmap.md`'s "Phase 5 ✅ Delivered" is accurate in the
bookkeeping sense (data model + helper + type switch) but the *swept queries
still treat the player as an axis-aligned box*. This is the single highest-leverage
gap in the system.

## Why It Matters: The Wallrun Symptom

`MovementSystem.cpp:761-815` contains a self-documented band-aid for AABB
collision on diagonal walls. The comment is worth quoting:

> The desired standoff must equal the Minkowski half-radius of the player AABB
> along the wall normal, plus a 1u slack — NOT `max(halfExtents.x, halfExtents.z) + 1`.
> The latter is only correct for axis-aligned walls. For a 45° wall the true
> envelope radius is `|n.x|*halfExtents.x + |n.z|*halfExtents.z` ≈ 22.6u, so the
> old 17u standoff placed the player ~5.6u INSIDE the Minkowski envelope every
> wallrun on a diagonal surface — the capsule visually clipped through the wall,
> and CollisionSystem couldn't catch the player when they slid past the bounded
> triangle's footprint.

The fix synthesizes a reactive inward velocity (gain = 10/s, capped at 100 u/s)
each tick to drag the player back to the correct envelope. That works most of
the time. It is also exactly the bug that vanishes when the player is a real
capsule: the envelope radius along *any* normal is just `r`. No diagonal-wall
math. No drift correction. No magic numbers.

## Tier-Ranked Techniques

The list below is graded by *load-bearing for our character controller*. We
already have the bulk of Tier 1's machinery from the roadmap; what's missing is
the shape and the activation of welding on the sweep path.

### Tier 1 — Mandatory for "never clip"

1. **Capsule-vs-Triangle closest-point + sweep**
   Ericson, *Real-Time Collision Detection* §5.1.10, §5.5.7. Closed-form
   segment-triangle distance. Swept query reduces to solving a quadratic in `t`
   for `dist²(seg(t), tri) = r²`. Robust, ~50 ops per triangle, no special cases
   for face/edge/vertex regions — they fall out of the segment-triangle distance
   function uniformly.

2. **Active Edge Method on the sweep path**
   Erwin Coumans, `btInternalEdgeUtility` (Bullet 2009). With capsule contact
   normals well-defined per region, replace the contact normal with the
   *adjacent active triangle's face normal* when the closest feature is a
   welded (inactive) edge or vertex. Eliminates "catch on internal edge"
   artifacts during wallrun. Currently disabled in our code because AABB
   contact regions are ambiguous; the disable becomes unnecessary with a
   capsule.

3. **Conservative Advancement (TOI iteration)**
   Mirtich 2000; used in Bullet/Jolt. Replace fixed-iteration bump loop with:
   compute clearance `d`, step `d / |v|`, repeat until `d < ε`. Naturally
   handles thin features. With our 800 u/s × 1/128 s ≈ 6.25 u step vs 16 u
   radius, 1–2 iterations suffice for typical motion. Keep depenetration as
   the post-step safety net.

4. **Tick sub-stepping when `|v|·dt > k·r`**
   Behind a feature flag. Kicks in only for grapple yank / knockback. Solves a
   class of bugs that don't yet manifest but will.

5. **Skin / collision margin**
   We have `k_pushback = 1/32` (Quake's `DIST_EPSILON`). Keep it.

### Tier 2 — Significant quality of life

6. **Speculative contacts** (Catto, Box2D Lite 2013).
   Generate contact constraints at projected positions, solve before motion.
   More elegant than bump-and-clip. Pays off when multiple simultaneous
   contacts (corner squeeze). Defer until we have a constraint solver hooked
   into character movement, which we don't today.

7. **Signed Distance Field static collision** (Unreal Nanite, Frostbite, Teardown).
   Bake the map to a 3D SDF (~8 cm voxel) at load. Capsule vs SDF = sample SDF
   at axis endpoints, subtract radius. Trade: bake time + per-map memory
   (~10 MB sparse, ~150 MB dense for a 100×100×30 m map). Consider for a v2 if
   BVH + capsule still gives grief; not first pass.

### Tier 3 — Not worth the complexity

GJK + EPA (only needed for arbitrary convex player shapes); GPU broad-phase
(irrelevant at our scale); XPBD character constraints (imperative kinematics
are easier to feel-tune).

## Phased Plan

Each phase is independently shippable. A and B are pure collision work; D–F
are pure movement work that depends on A–B; C is independent.

### Phase A — Capsule swept + depenetration vs all primitives

Add capsule analogues alongside the existing AABB functions; route the
player through them based on `CollisionShape::type`.

New functions in `physics/`:

- `sweepCapsuleVsPlanes(capsule, start, end, planes) → HitResult`
- `sweepCapsuleVsBox(capsule, start, end, WorldAABB) → HitResult`
- `sweepCapsuleVsBrush(capsule, start, end, WorldBrush) → HitResult`
- `sweepCapsuleVsCylinder(capsule, start, end, WorldCylinder) → HitResult`
- `sweepCapsuleVsSphere(capsule, start, end, WorldSphere) → HitResult`
- `sweepCapsuleVsTriangle(capsule, start, end, tri, edgeFlags, vertFlags) → HitResult`
- `sweepCapsuleVsTriMesh(capsule, start, end, WorldTriMesh) → HitResult`
- `depenetrateCapsuleVs*` for the same set.

Where `capsule` carries `radius`, `halfHeight`, axis orientation (gravity
direction). The capsule's swept AABB (for BVH culling) is its tight enclosing
box at `start`, expanded by `|v|·dt` along the motion direction — the same
`sweptAABBOverlapsAABB` test, fed by `enclosingAABB(capsule)`.

`sweepAll()` and `depenetrate()` in `SweptCollision.cpp` and
`CollisionSystem.cpp` get a dispatch on `shape.type`. AABB code paths stay for
non-player entities (projectiles, debris, ragdoll body parts).

Acceptance: a synthetic test scene with a 45° wall, an outer corner pillar,
and a series of coplanar quads. The player slides along each without the
inward-drift correction firing.

### Phase B — Active edge filtering on sweep + closest-point BVH

Goal: provide the per-edge neighbour data and the closest-point query that
Phase D wallrun needs to walk the surface as a manifold.

**Implementation outcome (as shipped).**

- `WorldTriMesh::edgeNeighbor` — 3 uint32 per triangle (UINT32_MAX = boundary
  or non-manifold) — populated as a side-product of `weldTriMesh()`.  The
  cooked-mesh format bumps to v2 to include the new array.
- `physics::closestPointOnMesh(segA, segB, maxDist, mesh)` — BVH-accelerated
  segment-to-mesh closest-point query.  Returns `{ found, dist,
  pointOnSegment, pointOnMesh, normal, triId, region }` with a capsule-axis
  convenience overload.  Built on `closestPointSegmentSegment` (Ericson
  §5.1.9) and `closestPointSegmentTriangle`.  `TriRegion` is promoted from
  the `.cpp` anonymous namespace to the public header so callers can branch
  on face / edge / vertex hits.
- Active-edge swap on the sweep path: **intentionally a no-op** in this
  architecture and documented as such in
  `TriMeshCollision.cpp:sweepCapsuleVsTriangle`.  The swap is the Bullet
  `btInternalEdgeUtility` recipe to snap an "in-between" contact normal to
  the closer adjacent face normal, but our sweep already returns the
  tested triangle's face normal as the contact normal — by construction
  there is no in-between direction to snap from.  A naive swap would also
  be unsafe for two-sided coplanar mesh hacks (separate-index reverse-
  winding escapes the welder's topological pair-up, but shared-index pairs
  welded together could see `dot(nA, nB) ≈ -1`, flipping the contact
  normal into the solid).  The neighbour data exists for Phase D's
  *direct* consumer — manifold edge crossings, not contact-normal
  correction.

Acceptance: a tessellated flat floor (8×8 quad grid, 128 triangles) — the
player walks across without seam catches (already true via Phase 2 welding;
preserved by capsule path).  Phase D acceptance — wallrun edge traversal —
will exercise the new data structures end-to-end.

### Phase C — Conservative advancement + sub-stepping (independent)

Replace the 4-iteration bump loop with conservative advancement. Add tick
sub-stepping when `|v|·dt > 0.5·r`. Both gated by feature flag for A/B
testing against the existing bump loop in known-good scenarios.

Acceptance: grapple-hook yank at >2000 u/s never clips through a wall.
Determinism hash matches between client and server.

### Phase D — Wall-attached kinematic frame

The wallrun rebuild. Replace `MovementSystem::handleWallRunning`'s
velocity-and-pray approach with explicit motion along the wall as a 2-manifold:

```cpp
struct WallAttachment {
    glm::vec3 anchor;     // closest point on wall surface
    glm::vec3 normal;     // surface normal at anchor
    glm::vec3 tangent;    // "forward along wall" — preserved across ticks
    uint32_t  tri_id;     // current anchored triangle
    TriRegion region;     // face / edge / vertex
};
```

Per tick:
1. Project player input + previous tangent + gravity-replacement onto the
   tangent plane → `desired_velocity`.
2. Walk `anchor` by `desired_velocity · dt` along the surface, staying on the
   same triangle if possible.
3. If the step crosses a triangle edge, consult adjacency:
   - **Inactive (welded coplanar)** edge: hop to neighbor without
     redirection. Same plane.
   - **Active convex** edge (outer corner): reflect tangent across the edge
     into the neighbor's plane, preserving magnitude. **Speed is preserved**;
     direction redirects geometrically. This is the seamless outer-corner
     traversal.
   - **Active concave** edge (inner corner): fold tangent into the new
     tangent plane; same magnitude-preserving geometry.
   - **Boundary** edge (no neighbor): end of wall; small outward ejection
     impulse and transition to airborne.
4. Place capsule at `anchor + normal·r`.
5. Run depenetration as safety net (no-op 99.9% of the time; if it fires,
   that is a *detectable* divergence between the manifold step and the
   collision world — gracefully drop wallrun rather than glitching visually).

The standoff band-aid at `MovementSystem.cpp:761-815` is **deleted** by this
change. The "standoff" is now a structural invariant (`anchor + n·r`), not a
PD-controlled error to be corrected.

Acceptance: player wallruns through a 90° outer corner without speed change
or visible direction snap. Same for an inner corner. The
`MovementSystem.cpp:761-815` block is gone.

### Phase E — Edge traversal feel polish

Tuning on top of Phase D:

- Cap redirection rate per tick (~90°) so a pathological tight pillar
  doesn't teleport the forward vector. At realistic geometry scales this
  bound is never reached.
- Camera roll keyed to the rate of tangent rotation (we already have
  `targetCameraTilt`).
- Drop the dot-product gating that currently terminates wallrun on side
  flip (`MovementSystem.cpp:727-731`); the tangent simply rotates through
  the dihedral.

### Phase F — Lucio-style entry impulse

Three new constants in `TitanfallConstants.hpp`:

```cpp
constexpr float k_wallrunEntryVerticalImpulse = 220.0f;  // u/s upward kick
constexpr float k_wallrunEntryHorizSnap       = 1.0f;    // preserve horiz
constexpr float k_wallrunVerticalDecayTau     = 0.45f;   // s; exp decay
```

At entry (`tryEnterWallrun`):

```cpp
vel.y += k_wallrunEntryVerticalImpulse;
vel.y = std::min(vel.y, k_wallrunEntryVerticalCeiling);
```

Per tick during wallrun:

```cpp
vel.y *= std::exp(-dt / k_wallrunVerticalDecayTau);
```

Net effect: ~1.0–1.4 m of climb in the first ~0.4 s, then flat slide. Matches
the Overwatch Lucio profile closely. The current `vel.y = 0.0f` "pure glide"
at `MovementSystem.cpp:860` is replaced by this decay.

Acceptance: video side-by-side with a Lucio wallride looks indistinguishable
in shape (climb-then-flat) within ~10% in apex height.

## Phase Dependencies

```
A ──▶ B ──┐
          ├─▶ D ──▶ E ──▶ F
          ┘
A ──▶ C   (parallel; C is independent of B/D)
```

Practical sequencing: A → B → C → D → E → F. C can slip after D if grapple
clipping is not yet a complaint.

## Risks and Open Questions

- **Capsule orientation under gravity flip.** Player code supports flipped
  gravity (`state.vis.gravityFlipped`). Capsule axis must rotate with gravity.
  The existing `minkowskiExtent(n)` helper assumes a Y-aligned capsule; this
  generalizes to a general axis with the same math (`r + halfHeight·|n·axis|`).
- **Crouch shape morph.** Today crouching shrinks AABB height by 14 u. With
  capsule, prefer shrinking `halfHeight` (keep `radius` constant) — mid-motion
  radius changes are nasty for collision continuity.
- **Networking byte-parity.** Both client and server compile
  `MovementSystem.cpp` and `CollisionSystem.cpp` identically. Everything in
  this plan stays deterministic. The `WallAttachment` becomes a new field in
  `PlayerSimState`; replicate via `PlayerVisState` if it needs to drive
  remote-player visuals, otherwise server-only.
- **Existing depenetration guards stay.** Velocity-coherent culling and
  welded-feature rejection in `aabbVsTriVoronoi` (rename to
  `capsuleVsTriVoronoi` for the capsule version) are the safety net of last
  resort against mesh data defects. Keep both guards active.

## References

- Ericson, *Real-Time Collision Detection* — §4.6 (BVH), §5.1.10
  (capsule-triangle distance), §5.5.7 (sweep), §9 (continuous collision).
- Coumans, "Continuous Collision Detection and Physics" (Bullet docs) +
  `btInternalEdgeUtility.cpp` — active-edge reference impl.
- Catto, "Continuous Collision" + "Speculative Contacts" (GDC 2013) — Box2D's
  approach, applicable conceptually.
- Mirtich, "Conservative Advancement" (2000) — TOI iteration.
- Quilez, capsule and triangle SDF articles — the per-primitive geometry.
- Respawn / Titanfall 2 talks on surface-attached movement — feel reference.
