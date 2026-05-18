# `group2` docs

Up-to-date technical documentation for the codebase. **Last full rewrite: 2026-05-16 on branch `core/collisions+wallrun`.**

The previous docs live under `docs/old/` for reference, but they're stale (some by months). The new set below was produced by reading the current source end-to-end.

---

## Start here

| Doc | What it covers |
|---|---|
| [architecture.md](architecture.md) | The big picture: three binaries, two threads/side, per-tick sequence on client + server, file layout, configuration |
| [potential-issues.md](potential-issues.md) | **Bugs and smells found while writing the docs.** Use this to prioritise the cleanup queue — including the open stair-climb / phasing / sticking regressions |

## Per-subsystem

| Doc | Scope |
|---|---|
| [ecs.md](ecs.md) | EnTT registry, all ~47 components, all ~25 systems, per-tick ordering, replication tuple |
| [physics.md](physics.md) | Movement state machine, Quake-style pmove, dynamics, solver, forces, wallrun |
| [collisions.md](collisions.md) | Capsule sweep + depen + closest-point, TriMesh BVH + welding, hybrid CA, stair handling, Phase A/B/C-deep status |
| [networking.md](networking.md) | UDP transport, fragmentation, snapshots + RLE delta, prediction + reconciliation, entity interpolation, lag compensation |
| [graphics.md](graphics.md) | SDL3 GPU renderer (mid-migration), per-frame structure, shader pipeline, viewmodel, skinned + particles status |
| [asset-loading.md](asset-loading.md) | GLB / FBX loading via Assimp, two-registry design, map loading, JSON sidecar (dead) |
| [animations.md](animations.md) | ozz-animation, 5-slot blender, AnimSnapshot replication, hitbox derivation |
| [particles-vfx.md](particles-vfx.md) | CPU pools + GPU storage/vertex buffers, 9-pipeline renderer, SDF text |
| [hud.md](hud.md) | 4-layer immediate-mode HUD, tween pool, offscreen MSAA target |
| [sfx.md](sfx.md) | SDL3 audio directly, 32-voice pool, dispatcher events + state-delta polling |
| [wwise-level-sound-engine-research.md](wwise-level-sound-engine-research.md) | Wwise concepts mapped to a practical data-driven/spatial sound-engine roadmap |
| [gameplay.md](gameplay.md) | Weapons, abilities, grenades, explosions, fire fields, powerups, match flow, ragdolls |

---

## Conventions in these docs

- **`path/file.cpp:line`** references point to a precise location in the current source tree.
- **Mermaid diagrams** render in GitHub / GitLab / VS Code preview.
- Anything in **`code font`** is verbatim from the source (constant name, type, file).
- **⚠ warnings** flag known bugs or gotchas covered in [potential-issues.md](potential-issues.md).
- "Last verified against source" at the top of each doc anchors it to the snapshot that was read.

## Maintaining these docs

When a subsystem changes meaningfully:

1. Update its dedicated doc (path:line refs first, prose second).
2. Update the **"Last verified against source"** line.
3. If you introduce or fix a known issue, update [potential-issues.md](potential-issues.md).
4. If the cross-subsystem story changes (e.g. you add a new top-level tick, change tick rate, add a process), update [architecture.md](architecture.md).

The docs in `docs/old/` should be considered historical artifacts. Don't try to keep them in sync.

---

## Quick-jump by area of concern

**I'm investigating a stair-climb / phasing bug** → [collisions.md](collisions.md) and [potential-issues.md §Collisions](potential-issues.md#collisions).

**I need to add a new ECS component or system** → [ecs.md](ecs.md), specifically the system-ordering section.

**I'm changing networking constants or adding a packet type** → [networking.md](networking.md).

**I'm adding a new weapon / ability / grenade** → [gameplay.md](gameplay.md) + the `WeaponConfig.hpp` / `GrenadeConfig.hpp` / `AbilityRegistry.cpp` files it links to.

**I'm working on movement feel** → [physics.md](physics.md), specifically the `TitanfallConstants.hpp` section.

**I'm wiring something into the renderer** → [graphics.md](graphics.md); watch for the migration TODOs and the skinned-not-rendered status.

**I'm writing or debugging shaders** → [graphics.md §5](graphics.md#5-shaders); note only 4 active shaders today.

**I'm adding sound** → [sfx.md](sfx.md); no spatialisation yet. For the Wwise-like roadmap, see [wwise-level-sound-engine-research.md](wwise-level-sound-engine-research.md).

**I'm authoring HUD widgets** → [hud.md](hud.md).

**I'm authoring particle effects** → [particles-vfx.md](particles-vfx.md).
