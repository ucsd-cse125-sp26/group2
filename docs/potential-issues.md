# Potential issues & bugs

Surveyed during the doc rewrite (branch `core/collisions+wallrun`, 2026-05-16). Items below were spotted **while reading the code to write the new docs** — they have not been verified by running the game. Treat each item as a hypothesis with a `path:line` pointer; reproduce before fixing.

> ⚠ **Partial fix landed during the doc rewrite.** Commit `a7a73a1` ("Implemented the first production-critical slice of the trimesh-first plan") changes `TriMeshCollision.cpp` to treat map triangles as **two-sided thin surfaces**, switches the capsule sweep to **conservative advancement** with finite edge/vertex blocking, and replaces face-normal solid-volume MTV with **surface penetration depth/normal**. This is intended to address items C-3 (face-normal orientation) and the thin-plane class of issues (C-4 in particular). A subsequent unstaged change to `SweptCollision.cpp` swaps the sphereCast trimesh path from AABB-conservative to a zero-height capsule sweep — addresses C-12. **Re-verify the affected items against the new code before acting on them.** The box-path issues (C-1, C-2, C-5) and the rest of the report were not touched by these commits.

Severity legend:

- 🔴 **Critical** — known regression / blocks functionality
- 🟠 **High** — silent wrong behavior / crash hazard / security
- 🟡 **Medium** — design smell or footgun
- ⚪ **Low** — dead code, magic numbers, doc rot

---

## Collisions

These map directly onto the user-reported regressions: **can't climb stairs · phase-through · stuck-in-thin-planes**.

### 🔴 C-1. `sweepCapsuleVsBox` rejects "starts inside expanded box" → stair climb broken

`src/ecs/physics/SweptCollision.cpp:424-426`:

```cpp
if (start.x >= k_expMin.x && start.x <= k_expMax.x &&
    start.y >= k_expMin.y && start.y <= k_expMax.y &&
    start.z >= k_expMin.z && start.z <= k_expMax.z)
    return result;   // no hit
```

Trace for the dev `testWorld()` stair (16u tall, top at y=16):

- Player on stair 0, `pos.y = 16 + 36 = 52`.
- Walk horizontally over stair 1 (top y=32). pos.y stays 52.
- `probeGround` calls `sweepAll(capsule, pos, pos-(0,26,0), world)`.
- Stair 1 expanded box: `k_expMin.y = 0-36 = -36`, `k_expMax.y = 32+36 = 68`. `start.y=52 ∈ [-36, 68]` → "inside" → **return no hit**.
- Only the floor plane at y=0 reports a hit. `resolveGround` snaps the foot to the floor — player teleports down through the stair.

Same issue in `sweepAABBvsBox:93`. TriMesh paths don't share this because `sweepCapsuleVsTriangle` reorients the normal toward the start.

### 🔴 C-2. `clearanceCapsuleVsBox` falsely flips to "penetrating" on top of a box

`SweptCollision.cpp:774-820` — uses axis-aligned overlap (not Minkowski-subtracted) for the gap test. A player standing on top of a box has axis overlap in all 3 axes (capMin.y < box.max.y < capMax.y), so `gapLen ≈ 0` → reports `distance = -16` (16u "depth"). Surfaces as a phantom CA fast-reject failure and may fire a depen pass that didn't need to fire.

### 🔴 C-3. `capsuleVsTriVoronoi` doesn't orient face normal → phase-through thin planes

`TriMeshCollision.cpp:472-534` — depen path uses the cooked face normal **as-is**. The sweep path (`:577-579`) orients it toward the start. For a one-sided triangle on the back side, depen pushes **further into solid**.

The velocity-coherent culling (`dot(vel, faceN) > 0 → reject`) is supposed to guard this, but it fails for stationary stuck players (vel ≈ 0).

### 🔴 C-4. Stuck-in-thin-planes: empty contact = empty unstick

`SweptCollision.cpp:1152-1153`:

```cpp
if (deepestCapsuleContact(...).valid)
    emergencyUnstick(...);
```

When **no contact at all** is found (e.g. welded coplanar pair rejecting both faces), `emergencyUnstick` is gated off. Player stays embedded. Should also fire when `clearanceCapsuleVsWorld` reports negative clearance.

### 🟠 C-5. `probeGround` reach is shorter than the capsule

`SweptCollision.cpp:937-957` — sweeps the full capsule down by `maxDistance = effStep + k_groundSnapDistance = 26`. But `capsule.minkowskiExtent(up) = r + h = 36`. The foot reaches only 26u below center, **less than the capsule's own extent**. Combined with C-1, descending stairs falls through to the floor plane.

### 🟠 C-6. Velocity-coherent culling suppresses legitimate floor depen during slow upward motion

`TriMeshCollision.cpp:340-341`:

```cpp
if (glm::dot(vel, faceN) > 0.0f)
    return false;
```

A player stuck below a floor with `vel.y = 1 u/s` (tiny) gets `dot(vel, +Y) = 1 > 0` → guard fires → depen returns no contact → player can't escape.

### 🟠 C-7. `clearanceCapsuleVsBrush` returns MAX of positive clearances

`SweptCollision.cpp:824-865` — brush surface distance is the **MIN** of per-plane half-space distances (tightest wall), not the MAX. Reporting MAX **overestimates** clearance — CA's fast-reject path may skip a closer wall and clip late.

### 🟡 C-8. `findWallAttachment` only iterates trimeshes

`MovementSystem.cpp:664-692` — wallrun's mesh closest-point query ignores `world.boxes/brushes/cylinders/spheres`. On `testWorld()` (all boxes) it always returns `{found=false}`. On mixed maps, wallrun "attaches" to trimesh and "detaches" when crossing onto a box wall.

### 🟡 C-9. Wallrun entry uses sphere-cast, sustain uses closest-point — inconsistent

Entry's `WallDetection::detectWalls` is sphere-cast (radius 12, range 35). Sustain's `findWallAttachment` uses `closestPointOnMesh`. A trimesh wall slightly beyond `35+12 = 47u` won't trigger entry even if it's within mesh reach.

### 🟡 C-10. Projectile depen has no iteration / convergence guarantee

`CollisionSystem.cpp:325-343` — calls `depenetratePlanes → Box → Brush → Cylinder → Sphere → TriMesh` in series. Each push, no iteration. If pushing out of box A pushes into box B, depen-B doesn't re-check A.

### 🟡 C-11. Projectile path uses AABB depen for capsule-shape projectiles

`CollisionSystem.cpp:761` calls `depenetrate(pos, vel, halfExtents, world)` regardless of `shape.type`. No capsule projectile exists today, but it's a footgun.

### 🟡 C-12. `WallDetection` hardcodes Y-down — broken in flipped gravity

`WallDetection.cpp:129-131`:

```cpp
const glm::vec3 k_feetPos = pos - glm::vec3(0.0f, halfExtents.y, 0.0f);
```

All probes (wallrun, climb, ledge, ground distance) probe the wrong end of the capsule when `gravityFlipped`.

### ⚪ C-13. `k_maxCAIterations = 8` is a magic literal

`CollisionSystem.cpp:533`. Should live in `PhysicsConstants.hpp` next to `k_maxDepenPasses`, `k_maxSubsteps`.

### ⚪ C-14. `k_pushback = 0.03125f` appears in 6 places

Files: `SweptCollision.cpp:21`, `CollisionSystem.cpp:74`, `TriMeshCollision.cpp:106`, `MovementSystem.cpp:621`. Multiple copies of the same magic 1/32 constant.

### ⚪ C-15. Stair-jumping detection threshold

`CollisionSystem.cpp:631`: `const bool jumping = k_vAlongUp > 10.0f;` Hardcoded 10 u/s. Should be tied to `k_jumpSpeed`.

### ⚪ C-16. Cooked-mesh format has no endianness guard

`CookedMeshFormat.cpp` reads/writes raw bytes — x86 little-endian only.

### ⚪ C-17. `BroadphaseTree::queryAABB` / `raycast` heap-allocate per call

`BroadphaseTree.cpp:333-336, 382-385`: `std::vector<int32_t> stack;` allocation per query. Not on the hot path today.

### ⚪ C-18. `SimdAabb::aabbBatchOverlap` has no live call sites

Written ahead of any consumer.

---

## Physics / movement

### 🟠 P-1. `PlayerSimState` is not replicated → reconciliation drifts

`ReconciliationSystem.hpp:20-27` acknowledges: coyote timer, jump cooldown, slide fatigue stay client-local. Edge-of-coyote-window inputs can desync. Likely contributor to the wallrun regression — client decides wallrun-entry from local `wallBlacklistActive`, server decides differently, reconciliation snaps to server result.

### 🟠 P-2. `runMovement` reads stale `grounded` (one tick old)

Movement runs BEFORE Collision. So Movement reads last tick's `grounded`/`groundNormal`. "Landing reset" (DJ refresh, blacklist clearing) fires one tick late.

### 🟠 P-3. `DynamicsSystem` hardcodes `-Y` gravity (ignores `gravityFlipped`)

`DynamicsSystem.cpp:45` — comment says intentional. Rigid bodies fall down while flipped-gravity player falls up.

### 🟠 P-4. `runCollision`'s grenade gravity also ignores flip

`CollisionSystem.cpp:757` — `vel.value.y -= physics::k_gravity * dt;` always.

### 🟠 P-5. Slide slope influence ignores flip

`MovementSystem.cpp:605-611` — hardcoded `k_gravity{0,-1,0}` vector. Sliding on a ceiling under flipped gravity gets no slope influence.

### 🟠 P-6. Ledge auto-mantle ignores flip

`MovementSystem.cpp:1130` — `vel.y = tms::k_ledgeJumpUpForce;` (no `* gravDir`). The `handleJump` branch (line 299) DOES multiply by `gravDir`. Inconsistent.

### 🟠 P-7. `computeWishDir` sign convention is suspect

`Movement.cpp:73-100` — comment at `:83` says "Camera right in world space is −X". At yaw=0, glm `cross((0,0,1),(0,1,0)) = (1,0,0)` — actual camera right is **+X**. Either the comment is wrong (likely), or A/D swap produces a real sign bug. Eyeball-test in-game.

### 🟡 P-8. `state.vis.sprinting` is dead code

`MovementSystem.cpp:262` unconditionally sets `sprinting = false`. `currentWishSpeed`'s sprint branch is unreachable.

### 🟡 P-9. `state.sim.canEnterSlide` never cleared

Only set true. Gate `!canEnterSlide` always succeeds.

### 🟡 P-10. `InputSnapshot::prevTickYaw`/`prevTickPitch` declared but unused

Never assigned, never read.

### 🟡 P-11. Wallrun constants declared but unused

`k_wallrunKickoffDuration`, `k_wallrunDetachThreshold`, `k_wallrunGripTime`, `k_wallrunGravityRampTime` — all dead. **No max-wallrun-time enforced.**

### ✅ P-12. `wallTriId` / `wallRegion` / `wallAttachmentValid` now consumed

Fixed on branch `core/collisions+wallrun`: wallrun sustain now passes stored mesh/triangle/feature identity back into
`findWallRunAttachment`, which walks `WorldTriMesh::edgeNeighbor` across triangle seams.

### 🟡 P-13. Stale wallrun standoff snap still writes `pos` directly

`MovementSystem.cpp:910-915` — claimed deleted in Phase D, but a single-pass standoff snap still lives there. Can cause one-frame jitter on stale `wallAnchor`.

### 🟡 P-14. HingeJoint angular constraint uses placeholder unit mass

`Joints.cpp:235-239` — explicit "refine later" comment. Inaccurate for non-unit inertia.

### 🟡 P-15. `physics::diag::setEnabled(true)` always on server

`ServerGame.cpp:94` — production CSV write. TODO to add CLI gate.

### ⚪ P-16. Tick rate hardcoded in 4+ files

`main.cpp:193` (server), `Game.hpp:122` (client), `Bot.hpp:41`, `LagCompensation.hpp:180`. No single source of truth.

---

## Networking

### 🟠 N-1. Magic bytes comment vs constant mismatch

`PacketHeader.hpp:22-23,48` — comment says `0x4732` ("G2"), constant is `0x3247`. Constant value works symmetrically; comment misleads anyone reading a wire dump.

### 🔴 N-2. TCP length-prefix DoS

`MessageStream.cpp:79-82` — reads `Uint32 length` and waits for `length` bytes with no upper bound. Malicious peer sends `[length=0xFFFFFFFF]` then withholds payload → `recvBuf` grows unbounded. Trivial DoS.

### 🔴 N-3. `InputArchive` throws into uncaught code

`RegistryArchive.hpp:53` throws `std::runtime_error` on short read. `Loader::apply` (`RegistrySerialization.cpp:292`) has no try/catch. A malicious server can terminate a client with crafted bad bytes.

### 🟠 N-4. Non-fragmented UDP keyframe double-applies

FULL keyframes are sent with `redundancy=2`. The non-fragmented fast path in `UdpEndpoint::sendFragmented:96-99` sends the same datagram twice with the same `(sequence, fragmentInfo=0)`. Receiver routes both into `udpRecvQueue_` → `dispatchMessage` twice → snapshot apply twice, doubled `recordInterpolationSamples`, doubled stats. Fragments dedup via reassembler slot; non-fragmented does not.

### 🟠 N-5. Default `snapshotHz` mismatch

`NetworkConfig.hpp:53` defaults to `128`. `ServerGame::init` parameter defaults to `32`. Live path passes the TOML value (128), but tests/test-harnesses bypassing NetworkConfig get 32. Footgun.

### 🟠 N-6. Unbounded queues throughout

- `OutboundQueue` (Server): no cap; slow client with replaceKey=0 entries piles up.
- `MessageStream::recvBuf`: no cap (see N-2).
- `Connection::reliableQueue`: unbounded if UDP silently dies while TCP alive.
- `Client::udpRecvQueue_`: no cap.
- `simLatOutbound_`/`simLatInbound_`: capped only by 200ms slider.

All are OOM hazards under slow-client or adversarial conditions.

### 🟠 N-7. Reconciliation silently skips missing ticks

`ReconciliationSystem.hpp:88-96` — if oldest ring entry > `ackedTick`, replay skips the gap and only resumes at the oldest available tick. Server position is correct (snapshot applied), but predicted velocity/state is wrong.

### 🟠 N-8. `acceptReliableSequence` doesn't reset on reconnect

Per-client sequence state on the client persists across a `Client::shutdown` + reconnect. Server's `reliableNextSequence` restarts at 0 — would be classified "older" and dropped until wrap.

### 🟠 N-9. No client-count cap

`Server::acceptClients` never refuses. Production deployment would expect a cap.

### 🟡 N-10. `JOIN_LOBBY`, `JOIN_FAILED`, `HOST_READY` defined but never used

Half-finished protocol features. Grep confirms zero senders/handlers.

### 🟡 N-11. `MessageStream::send` doesn't check first write return

`MessageStream.cpp:28` — if size-prefix write succeeds but payload write fails, the peer blocks waiting for payload (see N-2 amplifier).

### 🟡 N-12. SHOT_DEBUG_REPORT payload size not capped

`Client.cpp:1052-1080` — `numTargets:u8 × numCapsules:u8` → up to ~2MB. Bounded by fragment math but a malicious server could synthesize an inflated payload that passes size checks.

### 🟡 N-13. `clientCountAtomic_` only updated in `flushAllOutbound`

Server's `getClientCount()` is at most one network-cycle stale post-connect.

### ⚪ N-14. Wire format implicitly little-endian — no byteswap

`PacketHeader.hpp:9`, `MessageStream::send` (line 28) — host-endian length prefix. ARM big-endian wouldn't interoperate.

### ⚪ N-15. ASSIGN_CLIENT_ID legacy 4-byte form still accepted

`Client.cpp:875` accepts both 4B and 8B forms. 4B form yields permanent UDP fallback (no `connectionId`). Should be removed.

---

## Gameplay

### 🔴 G-1. `runPowerups` is doubly-broken

`src/ecs/systems/PowerupSystem.cpp:48-53`:

```cpp
for (ActivePowerup powerup : powerups.active) {  // value copy
    powerup.timeRemaining -= dt;                  // writes copy
    if (powerup.timeRemaining <= 0) {
        removePowerup(powerups, powerup.type);    // mutates during iteration
    }
}
```

Two bugs: copy-by-value loses the decrement; `removePowerup` does `std::erase_if` while iterating. **Powerups never expire** naturally.

### 🔴 G-2. `Shield` powerup never applied

Pickup adds it to `PowerupState.active`; nothing reads `PowerupType::Shield` anywhere. Pure no-op.

### 🟠 G-3. `applyDamage` crashes if killer lacks `PowerupState`

`PlayerStatusSystem.cpp:297` — `registry.get<PowerupState>(killer)`. Killer that's not a player (world hazard, projectile after owner disconnect) → SIGSEGV.

### 🟠 G-4. `applyDamage` crashes if killer lacks `AbilityState`

`PlayerStatusSystem.cpp:295` → `updateAbilityLevel` does `registry.get<AbilityState>(player)`. Same crash class.

### 🟠 G-5. `handleDeath` crashes if killer entity invalid

`PlayerStatusSystem.cpp:232-233` — `registry.get<ClientId>(killer)` / `Health`. No `registry.valid(killer)` check. Killer disconnects between damage queue and resolution → SIGSEGV. (Explosion path has a fallback to victim-as-killer; other paths don't.)

### 🔴 G-6. Ragdoll entities never destroyed

`runRagdolls` only ticks age. `spawnRagdoll` early-returns if `Ragdoll` already present, so respawn keeps the original corpse, never spawns new ones, and the first death's bones persist forever in the world.

### 🔴 G-7. `MatchController::winnerId` never set

`MatchController.hpp:37` declares it; only assigned `-1`. `handleWinCondition` correctly flags `PlayerMatchStats::hasWon`, but never bridges into `winnerId`. Clients see -1 in FINISHED packet.

### 🟠 G-8. `handleRespawn` doesn't reset `AbilityState` or `PowerupState`

Ability level + active powerups persist across deaths.

### 🟠 G-9. `MatchSystem::resetStats` doesn't reset Health/AbilityState/PowerupState on match end

Players carry wounds, ability level, powerups into the next match.

### 🟠 G-10. Combat is allowed in LOBBY/COUNTDOWN

`MatchController.cpp:19` comment acknowledges this — no system gates weapon/ability on `MatchPhase`. Players can rack up `kills ≥ 10` during LOBBY; once IN_PROGRESS starts, FINISHED triggers immediately.

### 🟠 G-11. `handleDeath` only drops PRIMARY + SECONDARY

`PlayerStatusSystem.cpp:204-205` — grenade slot never dropped. Killer can't pick up victim's grenades.

### 🟠 G-12. `applyHeal` overwrites armor instead of adding

`PlayerStatusSystem.cpp:44-52`:

```cpp
if ((playerHealth.health + amount) > systems::healthMax) {
    amount -= systems::healthMax - playerHealth.health;
    playerHealth.health = systems::healthMax;
    playerHealth.armor = amount;   // ← assigns
}
```

If armor was 60 and overflow is 30, armor becomes 30 (lost 30!). Should be `armor = min(armor + amount, armorMax)`.

### 🟠 G-13. `WeaponSpawnerSystem::checkForPlayers` doesn't check dead state

`WeaponSpawnerSystem.cpp:37-53` — dead players (with `RespawnTimer`) can still trigger overlap pickup. `DroppedWeaponSystem::tryPickup` correctly checks `pvis.isDead`; this one doesn't. Same in `PowerupSpawnerSystem::checkForPlayers`.

### 🟠 G-14. `WeaponSpawnerSystem` re-sets `hasWeapon=true` every tick after cooldown

`WeaponSpawnerSystem.cpp:95-100` — else branch every tick. Idempotent for the flag but means no "rising edge" for spawn-VFX/SFX. Cosmetic.

### 🟠 G-15. Dead players are pushed by explosion knockback

`ExplosionSystem.cpp:106` calls `physics::forces::applyImpulse` unconditionally. `applyDamage` early-returns on dead, but knockback runs. Dead bodies fly when hit by explosions — though respawn picks a new position so the drift is harmless.

### 🟠 G-16. Server-side `DashAbility` is never registered

`ServerGame.cpp:105` only registers GrappleAbility. If `AbilityState.primary = Dash`, activation silently fails.

### 🟠 G-17. `DashAbility::activate` body is a Grapple copy-paste

`DashAbility.cpp:64-88` — sphereCast forward 500u, sets `vis.grappleActive`, `sim.grapplePullDir`. Stub never finished. Hardcoded 500u, ignores `tms::k_grappleMaxRange`.

### 🟠 G-18. `AbilityState::primaryCooldown`/`primaryActive`/`secondaryCooldown`/`secondaryActive` are dead fields

Declared, read by `DashAbility::canUse`, **never written** anywhere. Live grapple cooldown lives on `PlayerSimState`.

### 🟠 G-19. `pendingLevel1`/`pendingLevel2` set but never cleared or consumed

Set true in `updateAbilityLevel`; nothing reads or clears.

### 🟠 G-20. Trigger events emitted but never consumed

`TriggerSystem` pushes events; consumer API `triggerEvents()` has zero call sites. Whole system is dead code.

### 🟡 G-21. `cycleGrenade` resets fire cooldown for the *current* grenade slot, not the type

`WeaponSystem.cpp:71-78`. Cycling type while held loses cooldown carry.

### 🟡 G-22. Hitscan ray origin is `eye`, not `muzzle`

`WeaponSystem.cpp:514` — eye-cast for hit, muzzle for VFX emission. Player can have their gun visually inside a wall while their head pokes around the corner — bullet still lands.

### 🟡 G-23. Reload is instant

`WeaponSystem.cpp:113-123` — explicit TODO; mag refills on next tick after empty-fire. Cosmetic.

### 🟡 G-24. `BeamState::fireCooldown` doubles as drain accumulator

Switching off beam to discrete weapon carries residual partial cooldown.

### ⚪ G-25. Many magic numbers

Muzzle offset `right*15 - up*8 + dir*5` (`WeaponSystem.cpp:174`), grenade shape `{5,5,5}` (`:436`), eye offset `0.75 * halfExtents.y`, suicide damage `999.0f` (`PlayerStatusSystem.cpp:364`), drop side offset `32.0f` (`:191`), `shotDebugAimMargin = 50.0f` (`WeaponSystem.cpp:370`), respawn 5s (`:220`), etc.

### ⚪ G-26. Ragdoll docstring contradicts implementation

`RagdollSystem.hpp:6-8` says "cone-twist joints stubbed in Phase 11 and substituted with point joints". Code actually uses ConeTwist (`:174-201`). Doc rot.

---

## Graphics / rendering

### 🔴 GFX-1. Animated characters never render

`SkinnedRenderer::draw` body is commented-out pseudocode (`:264-316`). `Game.cpp` never calls `renderer->skinned().setRig(...)` or `setFrame(...)`. Net: animated players are **invisible** in the new renderer.

### 🔴 GFX-2. Particles never render

`Game.cpp:201` has `// TODO(renderer-migration): renderer->setParticleSystem(&particleSystem);` commented out. `NewRenderer::setParticleSystem` captures the pointer but ignores it.

### 🟠 GFX-3. `excludeNodesContaining` parameter is silently ignored

`NewRenderer::loadSceneModel:476` accepts it; never forwarded. `Game.cpp:253-257` passes `visualExclude = "COL_"` expecting collision-only nodes to be skipped. Actual filter uses `AssetLoader.cpp:155-157` checking metadata `entity_type` / `is_collision` — different mechanism.

### 🟠 GFX-4. Texture set leaks on shutdown

`NewRenderer::quit` walks `Asset::meshes_` but **never iterates `Asset::textures_`**. Every `SDL_GPUTexture*` leaks at exit. Global static maps also never cleared.

### 🟠 GFX-5. First-frame model upload is synchronous

`loadSceneModel` ends with `SDL_SubmitGPUCommandBuffer + SDL_WaitForGPUIdle` per model. ~10 models × Assimp FBX parse on the main thread → seconds of frozen window at startup.

### 🟠 GFX-6. `setVSync`, `requestScreenshot`, `loadHDRSkybox`, `scanHDRFiles`, `setModelEmissive`, `setPointLights` are stubs

Data-capture only. 14 `TODO(graphics)` markers in `NewRenderer.cpp` + `SkinnedRenderer.cpp`.

### 🟠 GFX-7. `geometry.frag` multiplies albedo by `normal*0.5+0.5`

`shaders-new/geometry.frag:31` — debug-style normal tint. Likely unintended in shipping.

### 🟡 GFX-8. Pipeline color target = swapchain format

LDR. The renderer advertises `getHdrFormat() = R16G16B16A16_FLOAT` but no pipeline targets it. Future post-FX work has to migrate.

### 🟡 GFX-9. Cull mode = NONE everywhere

`Boilerplate.cpp:216` — doubles fragment work, masks modeling bugs.

### 🟡 GFX-10. Default texture path hardcoded

`NewRenderer.cpp:101`: `"assets/404.jpeg"` — fails if CWD wrong relative to `SDL_GetBasePath()`.

### 🟡 GFX-11. `loadHDRSkybox` always returns false

15 HDRIs in `assets/uploads_files_812442_HdriFree/` — picker UI would always fail.

### ⚪ GFX-12. `AssetLoader` logs Assimp errors even on success

`AssetLoader.cpp:21` — `SDL_Log("Assimp error: %s", importer.GetErrorString())` on every load.

### ⚪ GFX-13. `loadMesh` dereferences `mNormals` without null check

`AssetLoader.cpp:39`. `aiProcess_GenNormals` is applied so this is usually safe; mesh with `mNumVertices > 0` and silent post-process failure would segfault.

### ⚪ GFX-14. No validation-layer hookup

`SDL_CreateGPUDevice(..., debug=false, nullptr)`.

### ⚪ GFX-15. Per-frame `std::cout` in `AssetLoader`

9 locations dump mesh-load info to iostream.

---

## Animations

### 🔴 ANIM-1. `CrouchWalk` and `CrouchWalkBackward` both point to backward FBX

`AnimationLibrary.cpp:122-129`:

```cpp
case ClipId::CrouchWalk:
    return "crouch/Walk Crouching Backward.fbx";
case ClipId::CrouchWalkBackward:
    return "crouch/Walk Crouching Backward.fbx";
```

Forward crouch is mapped to the backward clip.

### 🟠 ANIM-2. `wallRunMirror` can persist past mode-exit due to floating-point `tBlend`

`CharacterAnimator.cpp:556-558` — mirror only clears once `tBlend ≥ 1.0`. Small `dt` interrupts can leave it stuck.

### 🟠 ANIM-3. Override clips loop forever

Slide / WallRun / Jump cycle via `overrideTime -= floor(overrideTime)`. A one-shot Jump clip will keep playing. **No animation-event system at all.**

### 🟡 ANIM-4. Inconsistent clip-file convention

Some return `"male_locomotion_pack/idle.fbx"`, others `"running_backward.fbx"` (no folder). Easy to break.

### 🟡 ANIM-5. Strafe swap is hardcoded around an FBX import bug

`pickStrafeClip:294-301` — comment admits the FBX clips are mis-named. Future asset swap will silently invert.

### 🟡 ANIM-6. `renderFromServer` uses local `wallRunSide` for mirror, not the snapshot's

`CharacterAnimator.cpp:600` — if server snapshot includes WallRun but local `inputs.wallRunSide` is None (interp-delay lag), mirror is wrong.

### 🟡 ANIM-7. Root-motion strip only locks joints containing "Hips"

`AnimationLibrary.cpp:310-316` fallback strips first translation-carrying joint. Rigs with `Pelvis + Hips_M` may still drift.

### 🟡 ANIM-8. No FBX axis correction

`FbxImportUtils.cpp:22-25` — just transposes. Non-Mixamo FBX with Z-up could come in oriented wrong.

### 🟡 ANIM-9. Skinning palette has no size validation

No check that `numInstances × numJoints × 64 bytes` fits the storage-buffer cap. ~100 players × 65 joints × 64 = 416 KB; well under any modern cap, but no guard.

### 🟡 ANIM-10. `setRig` calls `SDL_WaitForGPUIdle`

Synchronous wait at startup. Called once, tolerable.

---

## Asset loading

### 🟠 AL-1. `gamemap::spawnPoints_` / `weaponSpawner_` / `powerupSpawner_` are mutable globals

`MapConfig.hpp:126-138` — populated as side effect of `loadConfiguredMap`. **Not cleared between calls.** `clientbot` calls `loadConfiguredMap` per-bot in one process → lists triple.

### 🟠 AL-2. `loadConfiguredMap` writes `spawn_points.txt` to CWD

`MapConfig.hpp:260-274` — file dropped next to the binary on Linux, may fail on macOS bundles.

### 🟠 AL-3. `assets/config/assets.json` is dead

Parsed by nothing. Delete or wire up.

### 🟡 AL-4. FNV-1a 32-bit hash collisions silently overwrite

`Asset::meshes_/models_/textures_/materials_` keyed by 32-bit FNV. Two filenames colliding overwrite each other with no log.

### 🟡 AL-5. `Asset::` global static maps leak across hypothetical SDK boundaries

Headers declare `inline std::unordered_map` at file scope. Test fixtures or "two windows" feature would conflate state.

### 🟡 AL-6. `loadMapCollision` and `loadSceneModel` paths use independent `SDL_GetBasePath` concatenation

Both agree today, but the duplication is fragile.

### 🟡 AL-7. Naming collision: `gamemap::WeaponSpawner` vs ECS `WeaponSpawner`

Two `WeaponSpawner` structs in two namespaces sharing the name — confusing.

### ⚪ AL-8. Duplicate / unused assets

`Apex_Legend_Wraith.glb` and `apex_legend_wraith(1).glb` are duplicates (22 MB each). Porsche `free_1975_porsche_911_930_turbo.glb` (74 MB) is committed but load commented out. `Standard_Run.fbx` unused.

### ⚪ AL-9. `MapConfig::traverseNodeTree` writes to `std::cout`

`MapConfig.hpp:149`. Dead path but in production header.

---

## HUD

### 🟠 HUD-1. `HudContext::icon` is a 1×1 white placeholder

`HudContext.cpp:329` — explicit TODO. SVG/atlas pipeline doesn't exist; `HudIcons.cpp` is the procedural workaround.

### 🟠 HUD-2. TAB / B key-down handler — focus-loss-on-held leaks state open

`Hud.cpp:59-82` — if focus is lost while TAB held, key-up never arrives → Scoreboard stays open.

### 🟠 HUD-3. KillFeed shows "ARC-9" for every weapon

`KillFeed.cpp:16-22` — `weaponCallsign` returns "ARC-9" unconditionally (no typed-map yet).

### 🟠 HUD-4. SDF atlas only bakes ASCII 32–126

No UTF-8. Names with accented chars / emoji render as gaps.

### 🟡 HUD-5. KillFeed uses literal "You"/"YOU" string match

`KillFeed.cpp:24-27` — brittle against server naming.

### 🟡 HUD-6. `DamageIndicator::arcs_` not capped

Append-only with no max-size; relies on each entry's `timer` to decay. Bullet-storm could grow it briefly.

### 🟡 HUD-7. HUD vertex buffer never shrinks

`HudRenderer.cpp:353-382` — power-of-2 growth, no shrink. One 200K-vertex frame permanently inflates.

### 🟡 HUD-8. HitMarker overwrites on multi-hit same-frame

Only the last event's `kind_` wins.

### 🟡 HUD-9. `HudWidget` is a struct with public fields + dynamic_cast in debug

`Hud::processEvent` uses `dynamic_cast<Scoreboard*>` / `<BuyMenu*>` to react to keys. Fragile to widget reordering.

### ⚪ HUD-10. BuyMenu is a stub

`BuyMenu.cpp:55-60` — hardcoded weapon list, no purchase wiring.

---

## Particles & VFX

### 🟠 VFX-1. `ExplosionEffect::pending_` is dead code

`ExplosionEffect.cpp:43-48` — comment admits "caller must handle"; nothing consumes `pending_`. Deferred 0.1s smoke spawn never fires.

### 🟠 VFX-2. `BulletHoleDecal` never killed

`BulletHoleDecal.cpp:29-35` — `update` fades opacity to 0 then keeps drawing α=0 forever. No kill path. Wasted vertices, not a visual bug.

### 🟠 VFX-3. `HitscanEffect`/`ImpactEffect`/`SmokeEffect` use bare `std::rand()`

Not seeded, low-quality, not thread-safe. VFX seeds also not synchronized between clients.

### 🟠 VFX-4. `ParticleRenderer::buildPipelines` silent failure

Always returns `true` even if individual pipelines fail to load. Missing shaders → no particles, no error.

### 🟡 VFX-5. `TracerEffect::detach` writes through `const_cast`

Document says safe, but API surface returns `const T*`. Refactor footgun.

### 🟡 VFX-6. Tracer entityToIdx_ rebuild is O(N·M) on burst kill

`TracerEffect.cpp:107-111`. 512 tracers expiring at once ≈ 250K ops/frame. Probably fine.

### 🟡 VFX-7. `ParticleSystem::quit` doesn't quit each effect

CPU pools survive across `quit()`/`init()` cycles. Reusing the system in one process would carry stale entries.

### ⚪ VFX-8. `HitscanEffect` `k_maxBeams=4`; null-deref if 0

Brittle to constant change.

---

## SFX

### 🟠 SFX-1. No 3D spatialisation

Every sound plays at master mix. Far-away explosions are as loud as nearby ones.

### 🟠 SFX-2. Per-shot `SDL_AudioStream` allocation/destruction

`play()` allocates fresh stream every call (`:182-231`). 10 streams/sec at rifle fire. Pre-allocated pool would be cheaper.

### 🟡 SFX-3. State-delta polling assumes one `LocalPlayer`

`SfxSystem.cpp:355-407` breaks after the first. Fine today.

### 🟡 SFX-4. Looped voice retirement vs single-shot stream confusion

`EnergyBeamLoop` is single playthrough manually stopped, not actually looped. API surface implies looping.

### ⚪ SFX-5. MP3 error path frees `info.buffer` unconditionally

`SfxSystem.cpp:412-505` — safe but unusual.

---

## Debug / FrameRecorder / Menus

### 🟡 DBG-1. `FrameRecorder::recordFrame` grows unbounded vector

CSV is only written on `stopRecording()`. Long sessions OOM. Process-kill loses all data.

### 🟡 DBG-2. `FrameRecorder::startRecording` uses `std::localtime`

NOLINT acknowledges `concurrency-mt-unsafe`. Single-threaded only.

### 🟡 DBG-3. Each menu manages its own ImGui frame

`Home.cpp:34-54`, `Lobby.cpp:111-149` each call `ImGui_ImplSDLGPU3_NewFrame + ImGui_ImplSDL3_NewFrame + NewFrame + Render`. Centralization in `App` would be safer.

### ⚪ DBG-4. App cleanup ordering inversion

`App.cpp:286-298` — `quit() → renderer.quit() → shutdownAfterRenderer()` is non-obvious because the second phase outlives the renderer GPU backend.

---

## Bot

### 🟠 BOT-1. `applyIncomingSnapshot` doesn't check `snapshotLoader_->map` failure

`Bot.cpp:217-251`. If mapping fails, `localPlayerReady_` stays false indefinitely.

### 🟡 BOT-2. `yawPhase + t * yawRate` grows without bound

`Bot.cpp:471-572` — AI mode pre-ready drifts yaw at `t * yawRate`. After 10 min, ~1e3 radians. Wasteful but functionally OK (sin/cos handle).

### 🟡 BOT-3. `getCurrentRttMs` reads float without atomicity

`Bot.cpp:365-373`. Tor reads have "negligible practical impact" per comment, but `std::atomic<float>` is free on x86_64.

### 🟡 BOT-4. CSV `fflush` per row throttles busy bots

`Bot.cpp:84-95`.

---

## Server / threading

### 🟠 SVR-1. Event drop on tick-budget overrun

`ServerGame.cpp:386-390` — if event drain takes too long, break out and drop remaining events. Disconnects under load could be missed → ghost players.

### 🟠 SVR-2. `runPlayerStatus` mutates registry during view iteration

`PlayerStatusSystem.cpp:333-368` — iterates `view<Player, InputSnapshot>` and inside the lambda `handleDeath`/`applyDamage` adds/removes many components. EnTT view stability risk.

### 🟠 SVR-3. `pendingShotIntents_` cap eviction is non-deterministic

`ServerGame.cpp:355-356` — `unordered_map::begin()` order is unspecified; cap-eviction may drop the freshest. Acceptable for telemetry.

### 🟡 SVR-4. `Server::poll()` is a no-op kept for compat

`Server.hpp:55`. Called from `ServerGame::run()` line 199 every tick. Could be removed.

### 🟡 SVR-5. `Server::networkLoop SDL_Delay(1)` resolution

Windows default timer res can sleep up to 16 ms. Outbound latency game-thread → wire can spike.

### 🟡 SVR-6. Random respawn point constructs fresh `std::random_device` per call

`PlayerStatusSystem.cpp:92-94`. Per-respawn is rare so OK.

### 🟡 SVR-7. `nextClientId` increments forever

2.1 billion clients before wrap. Not a real-world concern.

### ⚪ SVR-8. `Renderer migration` TODOs across 4 sites in `Game.cpp`

`:201, :260, :291, :3380`. Partial migration.

---

## Cross-cutting / architectural

### 🟡 X-1. Singleton `physics::activeWorld()` couples to single-map-per-process

No clean reset. Multi-map / map-reload would need rework.

### 🟡 X-2. `runInputReceive` does no length / alignment check

`InputReceiveSystem.hpp:16-24` — `static_cast<const InputSnapshot*>(data)`. Trusts caller. Server's INPUT parser does the check, but the helper is callable from elsewhere.

### ⚪ X-3. CMake exposes `USE_ENTT` toggle in README, but `Registry.hpp` is unconditional

README claims a stub mode; actual code always uses `entt::registry`.

---

## What to fix first

If you only have time for a handful:

1. **C-1 / C-2 / C-5** (stair climb) — root-cause the "starts inside" rejection and the clearance sign error.
2. **C-3 / C-4** (thin-plane phasing / sticking) — orient face normal toward sweep start in depen; fire emergencyUnstick on empty-contact as well as oscillation.
3. **G-1** (`runPowerups`) — trivial 3-line fix.
4. **GFX-1 / GFX-2** (skinned + particles never render) — unblocks visible game state.
5. **N-2 / N-3** (TCP DoS, exception-on-bad-bytes) — security hardening.
6. **G-3 / G-4 / G-5** (`registry.get` on possibly-invalid killer) — crash hardening; switch to `try_get` or `registry.valid`.

Several other items are dead code or smells that can be cleaned up opportunistically.
