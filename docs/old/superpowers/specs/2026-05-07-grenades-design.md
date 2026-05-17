# Grenades — Design Spec

**Date:** 2026-05-07
**Status:** Approved (awaiting implementation plan)
**Scope:** Three grenade types (HE, Molotov, Impulse) wired to a single grenade slot on key `3`. Server-authoritative simulation, infinite ammo for v1, placeholder visuals.

---

## 1. Goals & Non-Goals

### Goals
- Add three distinct grenade types with clearly differentiated feel:
  - **HE** — frag grenade. Bounces, ~3 s fuse, lethal explosion.
  - **Molotov** — incendiary. Impact-detonates, leaves a damage-over-time fire field.
  - **Impulse** — movement/utility. Sticks where it lands, ~1 s fuse, big knockback, no damage.
- Single keybind (`3`) for both selection and type cycling — no extra HUD complexity.
- All per-type values live in **one config table** so adding a 4th type or rebalancing is a one-row edit.

### Non-goals (deferred)
- Carry counts, pickups, BuyMenu integration — infinite supply during dev.
- Cooking (hold-to-pre-fuse).
- Bespoke fire shader; 3rd-person throw animations.
- Client-side throw prediction — same server-auth model as rockets (~1 RTT lag).

---

## 2. Module Boundaries

The work splits into four largely independent units:

| Unit | Responsibility | Depends on |
|---|---|---|
| **Config** | Static data table for grenade tuning | nothing |
| **Inventory & Input** | Track active grenade type, handle key `3` | Config, `WeaponState`, `InputSnapshot` |
| **Throw & flight** | Spawn projectile, bounce/stick/fuse physics | Config, `Projectile`, `CollisionSystem` |
| **Detonation** | Explosion (HE/Impulse) or fire field (Molotov) | `ExplosionSystem`, new `FireSystem` |

Each unit can be implemented and unit-tested in isolation.

---

## 3. Data model

### 3.1 `WeaponType` enum extension

Append three entries:
```cpp
enum class WeaponType : uint8_t {
    Rifle, Rocket, RailGun, EnergyGun,
    HEGrenade, Molotov, Impulse,   // NEW
    Count                          // sentinel for table sizing
};
```
Existing `WeaponConfig`/`ProjectileConfig` arrays grow accordingly. Grenades use only `ProjectileConfig` (their `WeaponConfig` entries are stub-zero — fire path branches on the new `GrenadeConfig`).

### 3.2 `GrenadeConfig` (new)

Single source of truth for all grenade tuning. One row per grenade type, identical shape so adding a 4th is trivial.

```cpp
enum class GrenadeDetonationKind : uint8_t {
    Explosion,   // HE, Impulse: queueExplosion()
    FireField,   // Molotov: spawn a FireField entity
};

struct GrenadeConfig {
    // Throw mechanics
    float throwSpeed         = 1500.0f; ///< Initial speed along throw direction (u/s).
    float throwPitchOffset   = 0.35f;   ///< Upward rotation applied to eye dir before throw (radians, ~20°).
                                        ///< Computed as: throwDir = rotate(eyeDir, eyeRight, -pitchOffset).
                                        ///< Negative because Y-up and pitch-up convention.
    float throwCooldown      = 0.4f;    ///< Min seconds between throws (uses existing WeaponState.fireCooldown).

    // Flight physics
    float fuseTime           = -1.0f;   ///< Seconds. <0 = impact-detonate (no fuse).
    float bounceRestitution  = 0.0f;    ///< 0 = no bounce, 0.5 = lossy bounce, 1 = elastic.
    bool  sticky             = false;   ///< If true, freezes velocity on first surface hit then ticks fuse.
    float maxLifeTime        = 8.0f;    ///< Hard timeout safety.

    // Detonation
    GrenadeDetonationKind detonation = GrenadeDetonationKind::Explosion;
    // Explosion params (used when detonation == Explosion)
    float damage             = 0.0f;
    float explosionRadius    = 0.0f;
    float damageFalloffExp   = 1.0f;
    float selfDamageMult     = 0.4f;    ///< Same self-damage philosophy as rockets.
    float maxKnockback       = 0.0f;
    float knockbackFalloffExp = 1.0f;
    // FireField params (used when detonation == FireField)
    float fireRadius         = 0.0f;
    float fireDuration       = 0.0f;   ///< Seconds the field persists.
    float fireDps            = 0.0f;   ///< Damage/sec to players inside.

    // Cosmetic
    int   modelId            = 1;       ///< Reuses rocket model for v1.
    glm::vec3 tint           = {1,1,1}; ///< RGB multiplier for projectile rendering.
};

const GrenadeConfig& getGrenadeConfig(WeaponType type);
```

### 3.3 `Projectile` component additions

Add three fields. All default to "rocket-like behavior" so existing rockets are unaffected.

```cpp
struct Projectile {
    // ... existing fields ...
    float fuseTimer       = -1.0f;  ///< Countdown; <0 means no fuse (impact-only).
    float bounceRestitution = 0.0f; ///< 0 = no bounce. Used by CollisionSystem.
    bool  sticky          = false;  ///< If true, sets vel=0 on first hit and starts fuse.
    glm::vec3 tint        = {1,1,1}; ///< Render tint (cosmetic).
};
```

### 3.4 `FireField` component (new)

```cpp
struct FireField {
    glm::vec3 position{0.0f};
    float radius = 0.0f;
    float remaining = 0.0f;     ///< Seconds left.
    float dps = 0.0f;
    float tickAccumulator = 0.0f; ///< Damage applied at fixed sub-intervals (e.g. 4 Hz).
    entt::entity owner = entt::null;
};
```

Replicated to clients via `RegistrySerialization` so they can render fire VFX.

### 3.5 `GrenadeInventory` component (new)

```cpp
struct GrenadeInventory {
    WeaponType currentType = WeaponType::HEGrenade; ///< Currently selected grenade.
    // counts[] deferred; infinite supply for v1.
};
```

Lives on each player. Persists across deaths (re-emplaced on respawn with default).

---

## 4. Input flow

### 4.1 New `InputSnapshot` field
```cpp
bool cycleGrenade {false}; ///< Rising-edge on key `3`.
```
Sampled in `InputSampleSystem` from `SDL_SCANCODE_3` with edge detection (matches existing `flipGravity` pattern).

### 4.2 `WeaponSystem` handler
```text
on cycleGrenade rising-edge:
    if WeaponState.current is NOT a grenade type:
        WeaponState.current = inventory.currentType
    else:
        inventory.currentType = next(inventory.currentType)
        WeaponState.current  = inventory.currentType
```

`next(type)`: HE → Molotov → Impulse → HE.

### 4.3 LMB while on grenade
`WeaponSystem` fire path branches: if `WeaponState.current` is a grenade type, call `spawnGrenade(player, getGrenadeConfig(type))` instead of the existing rifle/rocket path. `spawnGrenade`:

1. Computes `throwDir = rotate(eyeDir, eyeRight, -config.throwPitchOffset)` — rotates the eye direction upward around the player's right-axis by the configured pitch offset. So a perfectly horizontal aim still launches the grenade in an arc.
2. Constructs a `Projectile` entity at the player's eye position with `velocity = throwDir * config.throwSpeed`, copying `fuseTime → fuseTimer`, `bounceRestitution`, `sticky`, and `tint` from the config.
3. Sets `WeaponState.fireCooldown = config.throwCooldown` to gate the next throw via the existing cooldown machinery.

---

## 5. Physics changes (`CollisionSystem`)

The existing projectile loop already handles rocket-style impact-explode. Three deltas:

1. **Fuse tick** — every tick, if `projectile.fuseTimer >= 0`, decrement by `dt`. If it drops ≤ 0, trigger detonation (see §6) and destroy the entity. This happens *before* movement so a stuck/cooked grenade detonates exactly at its current pos.

2. **Bounce** — when sweeping detects a hit and `projectile.bounceRestitution > 0`:
   - Reflect velocity: `vel = (vel - 2·dot(vel, n)·n) * restitution`.
   - Position is moved to the hit point (not destroyed).
   - Continue the bump loop with remaining time.
   - **Do NOT** trigger detonation on bounce — only on fuse expiry.

3. **Stick** — when sweeping detects a hit and `projectile.sticky == true`:
   - Set `vel = (0,0,0)` and snap to hit point.
   - Set `sticky = false` (consume one-shot behavior so subsequent ticks don't keep snapping).
   - If the projectile's `fuseTimer` was negative (i.e. wasn't ticking), set it to the configured `fuseTime` to start countdown now.

Existing impact-explode (rocket, molotov) path is unchanged: when neither `bouncy` nor `sticky` is set, the projectile destroys on hit and triggers detonation.

---

## 6. Detonation

Branch on the grenade's `GrenadeDetonationKind`:

### 6.1 `Explosion` (HE, Impulse)
Calls existing `queueExplosion(...)` with all values straight from `GrenadeConfig`. No code changes needed in `ExplosionSystem` — it's already general (recently extended for falloff exponent + self-damage + knockback).

### 6.2 `FireField` (Molotov)
- Spawns a new `FireField` entity at the impact position.
- New `FireSystem` runs each server tick:
  - For each `FireField`, decrement `remaining` by `dt`. If ≤ 0, destroy.
  - Tick damage at fixed sub-intervals (e.g. every 0.25 s = 4 Hz, configurable) to avoid floating-point drift across framerates: while `tickAccumulator >= tickPeriod`, find players within radius, call `applyDamage(dps * tickPeriod, ...)`, subtract `tickPeriod` from accumulator.
  - **Player-AABB test**: same closest-point logic as `ExplosionSystem` for consistency.
  - **Self-damage**: `selfDamageMult` applied if victim == owner (parity with rockets).
  - **No knockback** from fire (per design).

`FireSystem` runs in `ServerGame.cpp`'s tick loop, near `runExplosion`.

---

## 7. Networking

- `Projectile` already replicates. New fields (`fuseTimer`, `bounceRestitution`, `sticky`, `tint`) are tiny (≤ 17 B/projectile/snapshot pre-delta-encoding) and add to its tuple in `RegistrySerialization`.
- `FireField` added to the replication tuple. ~24 B/field/snapshot. Handful of fields max at any time — negligible.
- `GrenadeInventory` is server-side only (currentType is also read by InputSampleSystem on the owning client — but the client tracks it locally without round-trip; server reconciles on next snapshot if they desync).
- `cycleGrenade` flag added to per-tick input payload (1 bit, packed with existing booleans).

---

## 8. Visuals (placeholder, v1)

| Type | In-flight model | Tint | On-detonate VFX |
|---|---|---|---|
| HE | Rocket model (id 1) | green | Existing `ParticleEffectType::Explosion`, default color |
| Molotov | Rocket model | orange | New `ParticleEffectType::Fire` (animated billboards on `FireField`) |
| Impulse | Rocket model | blue | Existing explosion VFX, blue-tinted shockwave |

`tint` field on `Projectile` is read by the renderer and multiplied into the model's base color in the existing projectile-render path.

The new `Fire` particle effect is the only bespoke art asset needed for v1. Implementation: stack of upward-rising flame billboards spawned periodically from each `FireField` while it's alive, GPU-side animated via UV scroll on an existing flame texture or a procedural shader stub. Detail of the fire shader is out of scope for this spec — placeholder billboards are acceptable.

---

## 9. Tuning table (single source of truth)

| Field | HE | Molotov | Impulse |
|---|---|---|---|
| `throwSpeed` | 1500 | 1200 | 1500 |
| `throwPitchOffset` (rad) | 0.35 (~20°) | 0.44 (~25°) | 0.35 |
| `throwCooldown` (s) | 0.4 | 0.4 | 0.4 |
| `fuseTime` | 3.0 | -1 (impact) | 1.0 (post-stick) |
| `bounceRestitution` | 0.5 | 0 | 0 |
| `sticky` | false | false | true |
| `maxLifeTime` | 8.0 | 5.0 | 8.0 |
| `detonation` | Explosion | FireField | Explosion |
| `damage` | 120 | n/a | 0 |
| `explosionRadius` | 200 | n/a | 350 |
| `damageFalloffExp` | 2.5 | n/a | n/a |
| `selfDamageMult` | 0.4 | n/a | 1.0 (no damage anyway) |
| `maxKnockback` | 500 | 0 | **1100** |
| `knockbackFalloffExp` | 2.0 | n/a | 1.5 |
| `fireRadius` | n/a | 250 | n/a |
| `fireDuration` (s) | n/a | 5.0 | n/a |
| `fireDps` | n/a | 30 | n/a |
| `tint` | green | orange | blue |

Reference: rocket is 200 dmg, r=250, knockback 800 — so HE is a smaller-but-still-deadly poke (120 dmg, r=200), Impulse is the dedicated movement tool with significantly bigger pop (1100 vs rocket's 800).

---

## 10. File-level deliverables

**New files:**
- `src/ecs/components/GrenadeConfig.hpp` — config struct + lookup
- `src/ecs/components/FireField.hpp`
- `src/ecs/components/GrenadeInventory.hpp`
- `src/ecs/systems/FireSystem.hpp` / `.cpp`

**Modified files:**
- `src/ecs/components/WeaponState.hpp` (or wherever `WeaponType` lives) — enum extension
- `src/ecs/components/Projectile.hpp` — 4 new fields
- `src/ecs/components/WeaponConfig.hpp` — extend arrays for new enum entries
- `src/ecs/components/InputSnapshot.hpp` — `cycleGrenade` flag
- `src/client/systems/InputSampleSystem.hpp` — sample `SDL_SCANCODE_3`
- `src/ecs/systems/WeaponSystem.cpp` — handle cycle key, branch fire path
- `src/ecs/systems/CollisionSystem.cpp` — bounce + stick + fuse logic
- `src/server/game/ServerGame.cpp` — call `runFireField` in tick loop, init `GrenadeInventory` on player spawn
- `src/network/RegistrySerialization.cpp` — replicate `FireField`, extended `Projectile`
- `src/client/particles/ParticleEvents.hpp` — add `Fire` effect type
- `src/client/particles/...` — minimal fire-billboard renderer

---

## 11. Extensibility notes

- **New grenade type**: add an enum entry to `WeaponType`, append a row to the `getGrenadeConfig` table, append a tint/effect mapping. No system code changes needed if behavior is expressible as "fuse + bounce + stick + (Explosion | FireField)".
- **New detonation kind** (e.g. EMP, smoke screen): add an entry to `GrenadeDetonationKind`, add a branch in the detonation switch + a new config sub-block. Each kind is isolated to one switch arm.
- **Carry counts**: when added later, `GrenadeInventory` grows a `std::array<int, 3>` and `WeaponSystem`'s spawn path checks/decrements before throwing. Doesn't touch flight/detonation code.
- **Cooking**: when added, `WeaponSystem` starts a `GrenadeCookState` while LMB held; on release, copies remaining cook time into `Projectile.fuseTimer` instead of using the config's full fuse. No physics changes.

---

## 12. Test plan (manual, v1)

- Cycle key works: press `3` four times, verify HE → Molotov → Impulse → HE.
- HE bounces off floor and walls, detonates after 3 s regardless of bounce count.
- Molotov breaks on first surface hit, fire field damages players inside for 5 s.
- Impulse sticks to first surface hit, detonates 1 s later, applies large knockback to nearby players.
- Self-damage: HE thrown at feet kills if no armor, ~survivable with armor (matches rocket-jump feel).
- Impulse thrown at feet pops player ~3× normal jump height, deals zero self damage.
- Fire field damage scales with proximity (closest-point AABB test).
- Multiplayer: all three replicate to spectators. Server is authoritative; predicted client throws will lag ~1 RTT.
