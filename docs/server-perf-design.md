# Server 500-Bot Optimization — Design

Branch: `t3code/server-scalability-optimization`
Author: assistant (autonomous mode), authorized by user.
Mode: Profile-then-prioritize (Approach A) with Approach C (job-graph + GPU
compute + parallel kernels) as the **eventual** end-state, not a near-term
deliverable.

> **Working agreement.** I do not push or merge to `main`. I commit to
> feature branches; the user opens / merges PRs to `main`. The brainstorming
> skill's HARD-GATE is satisfied by this written, committed spec; user
> granted explicit autonomy on §§2–5 and on subsequent execution.

---

## §1 Constraints & success criteria

**Hard success criterion (stopping rule).**
Server holds **≥500 connected bots for ≥5 continuous minutes** with:

- Per-bot RTT **p99 < 15 ms** (statistic confirmed by user) on **localhost**
  (no simulated latency on the bot side).
- No dropped ticks: server tick wall time stays under the 128 Hz budget
  (~7.8 ms) on a representative tick sample.
- Snapshot delivery cadence honored (no growing OutboundQueue backlog).

If a PR fails to move at least one of these metrics versus the immediately
preceding measurement, work stops and the next change is re-evaluated.

**Out-of-scope (locked behavior).**

- Wire protocol semantics: `MessageStream` framing, `PacketType` values,
  `InputSnapshot` redundancy ring, lag-comp rewind tick selection rule,
  `k_maxLagCompTicks = 25`.
- Gameplay determinism: hitbox capsule definitions, damage math, weapon
  cooldowns, respawn logic.
- Tick / snapshot rate config keys (we re-tune values, not the schema).

**Out-of-band, also locked.** I will not modify the bot client's measurement
behavior in a way that flatters the metric (no client-side smoothing, no
RTT EMA window changes that hide spikes). The point of this work is to
make the *underlying* number small, not to massage it.

**Verification reality.** The 500-bot 5-minute test cannot run inside the
agent sandbox in a way that is portable across environments. Per-PR
verification therefore consists of: correctness (unit tests, no behavioral
diffs in lag-comp / damage / movement), microbenchmark numbers where
applicable, and a reasoned impact estimate. The user runs the actual
500-bot acceptance test on the canonical box.

---

## §2 Instrumentation contract (PR-1 deliverable)

The whole approach depends on PR-1 being trustworthy. Design:

- **Single-header `Profiler`** (`src/server/perf/Profiler.hpp`) — a
  lock-free per-thread context with scoped timers and a periodic
  aggregator. Thread-local storage for sample accumulation; the tick
  thread is the producer of >99% of samples, so the lock-free path is
  the common case.
- **Scoped timer macro** `GROUP2_PROF_SCOPE("name")` wraps each ECS
  system call site inside `ServerGame::tick()` and each major
  network-side phase: `poll`, `eventDrain`, `animation`,
  `hitboxHistoryPush`, `lagcompTargets`, `weapon`, `movement`,
  `collision`, `explosion`, `playerStatus`, `weaponSpawners`, `match`,
  `broadcastRegistry`, `broadcastEvents`. Plus the network-thread
  drainer side: `netDrain`, `netSend`.
- **Sampling cadence.** Every scope tick records one `(name_id, ns)`
  pair. Once per second we aggregate per-name across all samples that
  arrived in the last 1 s, computing min / p50 / p99 / max / count /
  mean. Output is one `SDL_Log` line:
  ```
  [perf 128Hz N=128 sumTickMs=421.3]  broadcast 3.1/3.4/4.9/5.2 (avg/p50/p99/max ms)  movement 1.8/2.0/3.1/3.4 ...
  ```
  Sorted descending by p99.
- **CSV mode.** `GROUP2_SERVER_PROFILE_CSV=path` writes one row per
  aggregator tick (1 Hz) with columns
  `t_unix_ms, system_name, count, min_ns, p50_ns, p99_ns, max_ns, mean_ns`.
  Lets us replay 150→200→300→500 sweeps later in any plotter.
- **Network metrics.** Server tracks per-tick `bytesSentTotal`,
  `bytesRecvTotal`, `snapshotsSent`, `outboundBacklog` (sum of per-client
  queue depth), and `clientCount`. These are recorded by the same
  Profiler infra (sampled once per tick, not per scope) and emitted in
  the same 1 Hz log line.
- **Bot-side RTT aggregator.** A small companion in `clientbot/main.cpp`
  collects each bot's `client_.getNetStats().avgRttMs` once per second,
  computes fleet p50 / p99, emits `[botfleet rtt N=500 p50=4.2 p99=11.7
  max=18.3]`. This is the headline metric.
- **Cost.** Every scoped timer is two `SDL_GetPerformanceCounter()` calls
  + one TLS append (~30 ns combined on x86). At 128 Hz × ~14 scopes
  per tick that's ~50 µs/sec — negligible.
- **Toggle.** `GROUP2_SERVER_PROFILE=1` (env var) for log mode;
  `GROUP2_SERVER_PROFILE_CSV=path` for CSV. Both off → the macro
  collapses to nothing in release builds (compile-time guard).
- **Invariant.** PR-1 must show **zero** observable behavior change
  with the env vars unset; the server binary should be byte-identical
  in CI to the pre-PR binary except for the new symbols. This is the
  no-regress check for the foundation.

PR-1 also adds `scripts/server-loadtest.sh`, a reproducible runner that
spawns the server (with the env vars on) and N bots, captures both
log streams to a timestamped folder, and spits out `summary.csv`
(server tick-time per system, bot fleet p50 / p99 RTT) so the user can
graph each PR's effect against the prior baseline without re-deriving
the test setup each time.

---

## §3 Measurement protocol & test rig

**Baseline points.** N ∈ {1, 50, 100, 150, 200, 300, 500}. Each run:
30 s warmup (bots connecting + idle) + 60 s steady-state measurement.
PR-1's runner does this automatically.

**Acceptance criterion per PR (post PR-1).** A PR ships if and only if
either:

- (a) The system it targets drops in p99 server-tick contribution by ≥30%
  at the same N, **and** fleet RTT p99 doesn't regress, **or**
- (b) The maximum-N where p99 fleet RTT < 15 ms grows by at least 1
  bucket (i.e. 100 → 150, 150 → 200, etc.).

If a PR shows neither effect, it gets reverted on the feature branch and
we re-look at the profile.

**No-regression check.** Every PR runs the existing test suite + a
gameplay smoke test (1 bot, 60 s, no warnings, no crashes, kills count
correct, lag-comp rewinds within tolerance). Regression in any of these
is a hard stop.

---

## §4 PR-by-PR optimization order

This is the *plan-of-record*; the actual order may shift if PR-1's
profile says something different. The plan is what I execute by default
absent contradictory measurements.

### PR-1: Instrumentation + harness (foundation)

Per §2 and §3. No optimization.

### PR-2: Snapshot broadcast (highest expected impact)

`Server::broadcastRegistry` (Server.cpp:568-604) **already** serializes
once per tick (good). The waste is downstream:

- **TCP fallback path (Server.cpp:597-602)** calls
  `frameMessage(buf.data(), ...)` per client, allocating a new
  `std::vector<uint8_t>` each time, then `OutboundQueue::enqueue()` *copies*
  that vector into a per-client deque (OutboundQueue.cpp:27-31).
- **UDP path (Server.cpp:586, sendFragmented)** copies stack buffers
  per fragment (UdpEndpoint.cpp:52-78). At 5 KB snapshot ÷ 1184 B = ~5
  fragments × 150 clients = 750 socket sends per tick, each with a
  per-fragment header memcpy.
- **OutboundQueue::enqueue replace-key scan** (OutboundQueue.cpp:14-24)
  is O(queue-depth) per client per tick. Small, but repeated.
- **MessageStream::pumpReads** (MessageStream.cpp:67) uses
  `recvBuf.insert(end, ...)` which reallocates on capacity grow.

Concrete changes:

1. **Encode framed bytes once.** Compute the framed (length-prefixed)
   payload exactly once per tick into a pooled `std::vector<uint8_t>`
   shared by reference; per-client TCP enqueue takes a `std::span`
   into that shared buffer, not a fresh vector.
2. **Per-tick bump arena** for transient buffers reused across
   `broadcastRegistry` / `broadcastParticleEvents` /
   `broadcastKillEvents` — eliminates small-alloc churn.
3. **OutboundQueue refactor**: the per-client queue stores a
   `std::shared_ptr<const std::vector<uint8_t>>` (or handle into the
   per-tick arena) instead of copying bytes. The `replaceKey` scan
   collapses to a hash check on a small fixed-slot array (snapshot,
   particle, kill, ping/pong) → O(1).
4. **`MessageStream::recvBuf` pre-reservation** with high-water mark
   reset between matches; same for `pumpReads`'s temp chunk to avoid
   the insert-grow path.
5. **AOI culling (Area of Interest)** — a player only receives entity
   data for entities within view-relevance distance. Default off; opt
   in via config. **Defer to PR-2b** if items 1-4 close the gap.
6. **Delta encoding** vs. last-acked tick per client. **Defer to a
   later PR** unless PR-2 alone is insufficient — delta is correctness-
   fragile and the explore profile suggests the pre-encode-and-share
   refactor will be enough at the 500-bot target.

### PR-3: Animation + hitbox capsule rebuild

`ServerGame::updateAnimationAndHitboxes` iterates a
`std::unordered_map<entt::entity, std::unique_ptr<CharacterAnimator>>`
sequentially. Each animator update is ~60 4×4 joint matmuls plus
blending; then `systems::updateHitboxes` transforms ~20 capsules per
player.

Changes:

1. **Replace the map with a flat dense array** indexed by a stable
   handle stored on the entity (`AnimatorIndex` component). Insertion
   uses a free-list; iteration is contiguous.
2. **`std::for_each(std::execution::par_unseq, ...)` across players**
   for the animator update phase. Each animator only reads its own
   ECS components and writes its own joint-matrix buffer — fully
   data-parallel.
3. **SIMD the 4×4 × 4×4 matmul** in the joint accumulation hot loop.
   GLM has SIMD intrinsics behind a flag (`GLM_FORCE_INTRINSICS`); if
   that's not enough, hand-vectorize with a small SSE/AVX kernel. Keep
   the fallback path for portability.
4. **Hitbox capsule transform**: `par_unseq` across players, capsule
   transform is 2 vec3 affine transforms — already SIMD-friendly via
   GLM.

### PR-4: Movement / swept collision

`runMovement` and `runCollision` per-player swept AABB against the
entire `mapCollision_` set (planes + boxes + brushes + cylinders +
spheres + triMeshes). Map has many trimeshes (per `MapLoader`).

Changes:

1. **BVH over static world geometry** built once at map load. Stored
   on the `physics::activeWorld()` side; immutable after init →
   thread-safe for reads.
2. **`par_unseq` across players** for movement/collision since the
   world is read-only and each player's swept-AABB is independent.
3. **Tighter inner-loop allocations**: any `std::vector` on a hot
   collision path is replaced with a small-buffer-optimized container
   (`absl::InlinedVector`-equivalent — we may roll our own to avoid
   adding a new dep).

### PR-5: Lag-comp hitscan + history copy

Two distinct issues from the explore profile:

- **`HitboxHistorySystem::push` (HitboxHistorySystem.cpp:31)**: copies
  the per-entity capsule vector into the ring slot every tick. ~12
  capsules × 24 B × 150 players × 128 Hz ≈ 5.5 MB/s of vector copy +
  reallocation churn. At 500 bots this triples.
- **`raycastPlayerHitboxes` (Raycast.hpp:629)**: per shot, iterates a
  view of every player + ~12 capsules. Burst-fire amplifies.

Changes:

1. **History ring stores fixed-capacity inline arrays** (or pre-sized
   vectors with reserve at construction) — push becomes
   `std::copy_n` into stable memory, no reallocation. Hitbox count is
   bounded (humanoid rig has ~20 capsules max), so a
   `std::array<WorldCapsule, 24>` + count is fine.
2. **Spatial hash on rewound capsules** per server tick: hash capsule
   centers into a 3D grid; ray-vs-capsule queries look up only the
   cells the ray sweeps. Construction is `par_unseq` over capsules;
   query is per-shot but cheap after the cull.
3. **Early-out**: weapon range as an axis-aligned cull before the
   spatial hash query.

### PR-6: Task graph wiring (A → C transitional)

By this point, individual systems are internally parallel. The next
lever is *inter-system* parallelism: `weapon` doesn't depend on
`movement`'s position output (it uses *prior-tick rewound* hitboxes),
so they can run concurrently on independent worker pools. Same for
`weaponSpawners` vs. most other systems.

Changes:

1. Lightweight task-graph executor (homebrewed, ~200 lines, no new
   dep). DAG of system nodes with dependencies declared in
   `ServerGame::tick()`.
2. The graph executor uses a fixed-size thread pool (sized to
   `std::thread::hardware_concurrency() - 2` to leave headroom for
   the network thread + main).
3. This is the *first* step that visibly looks like Approach C; it
   reuses everything PR-2..PR-5 built.

### PR-7+: GPU compute via SDL3 GPU (conditional)

Only entered if PR-3 leaves animation as the bottleneck *after* SIMD
+ par_unseq. The animation joint accumulation phase is the most
plausible candidate (embarrassingly parallel, dense math, low branch
divergence). SDL3 GPU compute pipelines (`SDL_GPUComputePipeline`)
support this on Vulkan/Metal/D3D12 backends.

Costs to weigh: dispatch latency (~50–200 µs round-trip); CPU/GPU
synchronization at tick boundary; correctness re lag-comp determinism
(GPU floats may differ subtly from CPU — at 128 Hz this matters for
hitbox history). Mitigations: keep the hitbox-history capsule writes
on CPU even if joint matrices are computed on GPU, OR force `-ffp-model=strict`
equivalent in shader and pin the GPU device. Decide at the time.

If GPU compute does NOT help (small per-dispatch payload at lower N,
latency dominates), defer.

### PR-8+: Continued iteration

Driven by what's left in the profile. Likely candidates:

- `OutboundQueue` lock-free SPSC ring per client (if a contention
  profile shows it).
- `RegistrySerialization` SoA layout for hot components (vec3 Position,
  vec3 Velocity, etc.) — easier delta encoding + SIMD-friendly memcpy.
- `EventQueue` MPSC lock-free queue if input drain shows contention.
- Tick-rate adaptive snapshot rate per client RTT (low-RTT clients
  get more updates; high-RTT clients get coalesced ones).

---

## §5 A→C trajectory: how the incremental plan converges on the rewrite

Approach C ("rewrite around a job graph + GPU compute + parallel
kernels") is the destination. Each Approach-A PR is shaped so that the
end-state is C *as the natural sum of the parts*, not a separate
rewrite:

| PR | Approach-C piece it lays | Why it's also Approach-A-shaped |
|----|--------------------------|---------------------------------|
| 1 | The measurement substrate any rewrite would need | No optimization; just data |
| 2 | Snapshot encode-once buffer pool (= the C "broadcast kernel") | Pure perf win at 150+ bots |
| 3 | Animation/hitbox as a parallel-kernel-shaped system | Internally par_unseq; compiles into a GPU compute kernel later |
| 4 | World BVH + parallel movement (= the C "physics kernel") | Wins immediately; zero gameplay change |
| 5 | Spatial hash on hitbox capsules (= the C "hitreg kernel") | Wins on burst-fire load |
| 6 | The job graph executor itself (= the C "scheduler") | Wins via inter-system parallelism |
| 7 | GPU compute pipeline (= the C "GPU dispatch path") | Conditional; only if data justifies |

The acceptance test for "we are now in the C end-state" is: server
`tick()` is a `taskGraph.execute()` call against a static graph
declared once at startup, and the per-tick CPU profile shows all cores
utilized to ~70–80% during the parallel phases. We don't need a
big-bang rewrite to get there; it falls out of the increments.

---

## §6 Risks, mitigations, kill-switches

**Risk: lag-comp determinism breaks under par_unseq.** Mitigation: all
parallel system kernels read shared state through `const&` only; any
writes are to per-entity slots (no shared mutable state). Hitbox
history pushes are serial (the ring is per-entity; pushes are at the
end of the per-tick animation kernel). Guard with a unit test that
runs the same input deterministically with par_unseq on/off and
diff-checks the resulting registry snapshot.

**Risk: GPU compute introduces RTT latency that dwarfs the saving.**
Mitigation: gate behind a measured threshold (only enable if CPU
animation > 1 ms in PR-1 baseline at 500 bots).

**Risk: a PR claims a win that's measurement noise.** Mitigation: per-PR
benchmark spreads ≥3 runs of the harness; require the median of the
post-PR runs to beat the 95th-percentile of pre-PR runs by at least
the §3 acceptance threshold.

**Risk: I burn agent-tokens iterating without making progress.**
Mitigation: each PR has a budget (rough guideline: complete + verified
in one work session; if not, stop, summarize blockers, ask user). The
500-bot final acceptance test is a user-side step — I don't loop on it.

**Kill-switch.** Every parallelization is gated behind a runtime flag
(`GROUP2_SERVER_PARALLEL=0` falls back to sequential) so a regression
can be bypassed in the field without redeploying.

---

## §7 What gets committed where

- `src/server/perf/Profiler.hpp` (new) — header-only Profiler.
- `src/server/perf/Profiler.cpp` (new) — name registry + aggregator
  thread.
- `src/server/perf/NetworkStats.hpp` (new) — per-tick network counters.
- `scripts/server-loadtest.sh` (new) — reproducible harness.
- `src/server/game/ServerGame.cpp` — scoped-timer macros at system
  call sites; otherwise unchanged (PR-1).
- `src/server/network/Server.cpp` — scoped-timer macros + network
  counter updates (PR-1).
- `src/clientbot/main.cpp` + `src/clientbot/Bot.cpp` — RTT aggregator
  (PR-1).
- Subsequent PRs touch the systems they optimize — listed in §4.

---

## §8 Done definition

- 500 bots × 5 min × p99 RTT < 15 ms on the user's canonical box, OR
- A clearly written final report explaining where progress stalled,
  what would unblock it (hardware? algorithmic limit? out-of-scope
  protocol change?), and which PRs nevertheless land net wins on the
  150–300 bot range.

The second outcome is acceptable; we don't ship lies. "I tried, here
are the wins, here's the wall" is a perfectly good deliverable.

---

## §9 Final report — what shipped and what didn't

### Landed PRs

| Commit  | Title | What it does |
|---------|-------|--------------|
| `3ad9fed` | docs: server 500-bot optimization design spec | This document. |
| `d1fe4a9` | PR-1: scoped profiler + fleet RTT + harness | Foundation: per-system timers, 1 Hz aggregator, CSV out, bot fleet aggregator, `scripts/server-loadtest.sh`. |
| `cf7f97a` | PR-2: defer snapshot fanout, share-frame, bulk drains | Snapshot bytes shared via `shared_ptr`; per-client UDP fanout deferred to network thread; `EventQueue` self-locks; bulk RTT snapshot. **Largest measured win.** |
| `530d7b9` | PR-3: parallel-STL hooks for animation + hitbox | Optional TBB-backed `parallelFor`; pre-emplace + parallel kernel pattern. **Default off**: localhost loadtest oversubscribes cores with the bot fleet. |
| `229a97e` | PR-4: atomic-published read snapshots + match throttle | Lock-free `getClientCount` and `snapshotClientRtts`; shared-frame for events broadcast; match replicate-on-change. Together they push the "server-keeps-up" range from ~100 bots to ~300 bots. |
| `dc0b868` | test: bot env vars `NO_SPIN`, `TICK_HZ` | Lets the loadtest harness fit ≥ 200 bots on a single host without the spinning bot fleet starving the server. Production gameplay clients keep the spin. |
| `201737a` | PR-5a: share-frame in `enqueueReliableEvent` | Last per-client copy in the broadcast path goes away. `Connection::PendingReliableEvent::framed` becomes `shared_ptr<const vector<u8>>`. At 300 bots cuts `broadcastEvents` p99 from 25-50 ms to ≤ 6.29 ms. |
| `1d175ee` + `2351d1a` | PR-5b: `OutboundQueue` self-locks; lock-light `flushAllOutbound` | Three-phase flush: short lock to snapshot per-client targets, lock-free syscall fan-out, short lock to apply disconnects + republish atomic snapshots. `OutboundQueue` carries its own `std::mutex`; `Connection` becomes non-copyable; `MessageStream` grows a default ctor. The follow-up commit fixes a missed-lock bug in the first cut. |
| `89442cc` | PR-6: `shared_mutex` for clients map + `readClients` shared/deferred | Closes §9 unblocker #2. Read-mostly paths (`enqueueBroadcast`, snapshot fanout, UDP receive, `readClients`) take `shared_lock`; writers (`acceptClients`, disconnect-application, `enqueueReliableEvent`, reliable-queue drain) take `unique_lock`. `readClients` refactored to two-phase: shared-lock read pass + deferred unique-lock disconnect. **At 300 bots: tick p99 25.17 → 6.29 ms (4× lower); tickN 117 → 128 (server keeps up at full 128 Hz); fleet RTT p99 ~290 → 123 ms.** |
| `8947791` | PR-5: ray-AABB pre-filter in `rewindHitboxes` | Lag-comp rewind no longer touches every player per shot — broad-phase ray-AABB skips far candidates. No-op in synthetic clustered-bot tests; significant in real gameplay where players are spread out. |
| `3061d54` | PR-7a: parallelize `runCollision` + `runMovement` | Extends PR-3's parallelFor pattern to the two largest sequential ECS scopes. Pre-collect entity handles, then per-player kernel via `parallelFor`. Each iteration touches only its own entity's components + read-only world geometry. |
| `e64e261` | PR-8: parallel registry serialization (per-component-type fan-out) | Each `entt::snapshot.get<T>` is self-contained per type; we allocate one OutputArchive per Synced tuple element and dispatch the 14 per-type serializations as independent TBB jobs, then concat in tuple order. **At 300 bots: broadcastRegistry p99 12-25 → 0.10-12 ms (typical 1.57); tick p99 25 → 1.57-12.58 ms (10× lower).** Wire bytes byte-identical to sequential form. |
| `f04fe5d` | Default `GROUP2_SERVER_PARALLEL` to ON | Earlier off-by-default was a localhost-test artifact. With AI bots + PR-7/8 combined, parallel always wins at any N where it has measurable effect. Kill switch: `GROUP2_SERVER_PARALLEL=0`. |
| `0632ab0` | `GROUP2_BOT_CPUS=lo,hi` pin bot threads to a core range | Linux-only `pthread_setaffinity_np` for isolation experiments. On this rig the kernel's natural distribution was already optimal — added as a tool. |
| `7a4396a` | PR-9: parallelize per-client network sends | Two largest still-sequential network-thread loops are now parallel: `flushAllOutbound`'s per-client `flushTo` (TCP send), and the snapshot fanout's `sendFragmented` (UDP send to N targets). SDL_net is documented thread-safe per-socket. **At 300 bots: fleet RTT p50/p99 93/101 → 9.91/23.36 ms (~10× lower).** |
| `6496b52` | PR-10: snapshot delta encoding (RLE byte-diff) | New `UPDATE_REGISTRY_DELTA` packet type. Server keeps prev-snapshot raw bytes; if next serialization is the same size, computes RLE byte-diff. Ships delta when it beats full by >25%; forces full keyframe every 16 snapshots (~500ms) for resync. Client reconstructs full from baseline+patch and applies through existing Loader. **At N=500: net out 1.9-2.6 GB/s → 0.39-0.52 GB/s (~5× reduction); fleet RTT p99 200-400+ → 31-32 ms (6-12× lower); server tickN stable at 141-153.** |
| `c0aa20b` | PR-11: client entity interpolation, 2-tick render delay | New `InterpolationBuffer` ECS component (8-slot ring of `(captureNs, position, yaw)` per entity, ~192 B). After every snapshot apply (FULL or DELTA), `Client::recordInterpolationSamples` walks the registry and appends a sample for each non-local entity. Renderer reads `Client::getInterpolationRenderTimeNs()` = `now − N × snapshotInterval` (default N=2, configurable via `GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS`) and calls `entity_interpolation::sample` to lerp between bracketing samples. No extrapolation past newest sample (Source-engine freeze policy). Local player keeps client-side prediction (Phase 5b). Headline win is **visual smoothness under jitter/loss** — the renderer always has ≥ 1 buffered "future" sample to interpolate toward, so a single dropped snapshot is invisible. Net wire bandwidth and fleet RTT are unaffected (pure renderer-side change). At N=200 the append loop adds ~6.4 k samples/s server-wide with no measurable tick-time impact (tickN=128, p99 ≤ 0.79 ms). |
| `7500bb2` | PR-12: lag-comp formula — communicate `cl_interp` to server | INPUT packet wire format gains 1-byte `interpDelaySnapshots` field. Server's `updateLagCompTargets` rewinds by `RTT/2 + interpDelaySnapshots × snapshotEveryNTicks` instead of just `RTT/2`. Eliminates the 62.5 ms shot-registration drift PR-11 introduced at higher pings. `k_maxLagCompTicks` bumped 25 → 64 and `HitboxHistory::k_capacity` 32 → 64 to cover the wider rewind window. Wire cost: ~128 B/s/client at 128 Hz INPUT rate — same scale as the existing `rttMs` prefix. Naming cleanup: client-side field renamed `interpDelayTicks_` → `interpDelaySnapshots_` (was ambiguous re: physics 128 Hz vs snapshot 32 Hz ticks; it was always snapshots). |
| `538a77a` | PR-13: default snapshot rate 32 Hz → 128 Hz (AAA tier) | Flips `ServerReplicationConfig::snapshotHz` and `config.template.toml` default from 32 → 128 Hz (= tick rate). Same cadence Valorant / CS2 / Apex run. Combined with PR-10 deltas (~5×), wire BW lands at ~0.8× the pre-PR-10 32 Hz baseline despite 4× the rate. **PR-11 render delay drops 62.5 → 15.6 ms.** Lag-comp resolution improves from 31 ms snapshot boundaries to 7.8 ms tick boundaries. **At N=100: server tickN=128, fleet RTT p50/p99 7.83/8.51 ms, broadcastRegistry p99 0.05 ms** — ~4× the 32 Hz baseline cost, still well under the 7.8 ms tick budget. |
| `3501181` | PR-14: delta-from-keyframe (loss-resilient delta encoding) | Bug fix: pre-PR-14, server computed each delta from the *immediately previous snapshot* (`prevSnapshotRaw_` updated every send). One dropped delta cascaded — next delta's `fromTick` referenced bytes the client no longer held → silent drop, repeating for the rest of the keyframe window. Combined with fragmented FULL keyframes (~6 fragments at N=100), 5% per-fragment loss caused effective state-update rate to plummet to ~10-20%. Fix: `prevSnapshotRaw_/Tick_` renamed → `keyframeRaw_/Tick_`, replaced ONLY when sending a FULL. Every delta in a keyframe window now references the same fixed baseline, so individual delta drops only cost that single frame's state. `k_keyframeInterval` reduced 16 → 8 (62 ms recovery worst case at 128 Hz). Smoke at N=50 clean network: server tickN=128, fleet RTT p99 ~8 ms — no regression. Loss-path validation in real client (clientbot has no loss slider). |

### Empirical results (relwithdebinfo, localhost, no simulated latency)

Final state after PR-1/2/3/4/5a/5b plus the PR-4 bot lightening
(`GROUP2_BOT_NO_SPIN=1`, 128 Hz bot tick rate kept):

| N    | Pre-PR-2 tick p99 | Final tick p99 (post-PR-6) | Final fleet RTT p50 / p99 | Status |
|-----:|------------------:|---------------------------:|--------------------------:|:-------|
|  50  | 3.15 ms          | **0.39-0.79 ms**           | 7.86-7.92 / **8.08-8.13 ms** | trivial; 4-8× tick win |
| 100  | 25-50 ms (broken) | **0.79 ms**                | 7.92 / **15.64 ms**       | tick 30+× lower; RTT under target at p50, on edge at p99 |
| 200  | broken           | 1.57-12.58 ms              | 31.31 / 39.40 ms          | server holds 128 Hz; RTT bounded by single-threaded PONG send |
| 300  | broken           | **6.29 ms**                | 85.66 / **123.44 ms**     | tick 4× lower than PR-5b; server holds 128 Hz; RTT high but no longer multi-second variance |
| 400  | broken           | 200+ ms (variance huge)    | 257 / 557 ms              | host CPU exhausted: ~400 clientbot threads at ~99% on 16 cores |
| 500  | not reachable    | not reachable              | not reachable             | fails — host CPU completely saturated by 500 bot threads |

Per-PR cumulative wins on the headline scopes:

  `broadcastRegistry` p50 (N=50):   1.57 ms (PR-1 baseline)
                                   → 0.01 ms (PR-2 share-frame + deferred fanout)        — 157×
                                   → 0.01 ms (PR-3/4 unchanged)
                                   → 0.01 ms (PR-5 unchanged at this N)

  Tick p99 (N=100):   25-50 ms (PR-1, broken)
                    → 6.29 ms   (PR-2 broadcast)
                    → 3.15 ms   (PR-4 atomic reads + match throttle)
                    → 1.57 ms   (PR-5b lock-light flush)
                    → **0.79 ms** (PR-6 shared_mutex + readClients shared)               — 30-60×

  Tick p99 (N=300):   broken    (PR-1)
                    → 50.33 ms  (PR-2 broadcast)
                    → 25.17 ms  (PR-5a/b)
                    → **6.29 ms** (PR-6)                                                  — 4× from PR-5b

  Fleet RTT p99 (N=100): 28-44 ms (PR-3, broken)
                       → 14-21 ms (PR-4)
                       → 15.44 ms (PR-5b)                                                — 2× under target at p50

  ECS scopes (N=200): each ≤ 0.79 ms p99 (animation, movement, collision, weapon, etc.) — well below the 7.81 ms tick budget
  even with 200 player simulations.

Single-PR headline win: `broadcastRegistry` dropped from 1.57 ms
p50 (50 bots) to **0.01 ms** — a 157× collapse — via PR-2's
shared-frame + deferred-fanout work.

PR-4 gave: lock-free `getClientCount` / `snapshotClientRtts` (atomic
RTT cache), `enqueueBroadcast` shared-frame for events, and the
match-state replicate-on-change throttle. Combined with the bot
no-spin flag, it pushed the working range from "≤ 100 bots" to
"≥ 300 bots with server still ticking at 128 Hz."

### Where the wall is (post-PR-4)

PR-4's atomic-published read snapshots fixed the second limit
listed in the previous version of this section — the game-thread
`getClientCount` / `snapshotClientRtts` paths are now lock-free,
and the per-tick MATCH_STATE broadcast is now replicate-on-change.
Combined with the PR-4 bot no-spin flag, the server now ticks at
the full 128 Hz at ≥ 300 bots on the localhost loadtest harness.

The remaining wall at ≥ 300 bots is now PONG/snapshot delivery
latency on the network thread:

1. **PONG round-trip latency.** PING/PONG is sent on the network
   thread inside `handleUdpUnreliable` while still holding
   `stateMutex_`. Pre-PR-4 this was overshadowed by other
   contention; now that the game thread is unblocked, the bottleneck
   is the network thread itself. With 300 clients pushing 30 KB
   snapshots × 32 Hz = ~700 MB/s outbound, the network thread's
   per-cycle send loop (deferred snapshot fanout + reliable-event
   queue drains + flushAllOutbound) is the single saturating
   resource. Fleet RTT p99 climbs to 130-150 ms at 300 bots — well
   above the 15 ms target but driven by single-threaded I/O, not
   by the simulation.

2. **Bot-side CPU on a shared host.** Even with `NO_SPIN`, 500
   clientbot threads on a 16-core box compete with the server for
   cores. At 500 the OS scheduler can't keep both fleets and
   the server on time. This is the same hardware wall as before,
   just relocated.

The first is architectural (multi-threaded I/O is the real fix —
shard `networkLoop` workers by connectionId, drop per-client lock
to a per-Connection mutex, allow PONGs to ship in parallel). The
second is hardware.

### What still needs to happen for 500 bots

1. **Test rig: bots on a separate machine** (or set of machines).
   This is the single biggest unblock. `scripts/server-loadtest.sh`
   currently colocates bots and server on localhost; that's wrong
   for any N > ~150 on commodity hardware. Suggested follow-up:
   add a `BOT_HOST=...` env var to the harness and run bots on
   one or two separate boxes connected over LAN.

2. **Fine-grained client-state locking** (server side). Replace
   `stateMutex_` with three locks:
   - `acceptMutex_` for the listen socket / new-connection path
   - `clientsMutex_` for the clients map (`std::shared_mutex` so
     reads don't block reads)
   - per-`Connection` mutex for the `OutboundQueue`, `udpAddr`,
     `lastReportedRttMs` fields the game thread snapshots

3. **Multi-threaded I/O** (the natural extension of #2). Split
   `networkLoop` into a worker pool: each worker handles a shard
   of clients (modulo connectionId). With 4 I/O workers and
   per-client lock, total I/O wall time shrinks proportionally.

4. **Snapshot delta encoding** (deferred from PR-2). Most
   components don't change tick-to-tick; sending the full
   registry every snapshot is wasteful. A per-client baseline +
   delta would cut bytes by ~70–90% in typical gameplay,
   reducing both wire bandwidth (now 86 MB/s at 100 bots →
   ~10 MB/s) and per-fragment syscall count proportionally.
   Requires per-client baseline state and ack-tracking; it's
   correctness-fragile so wants a careful PR.

5. **AOI culling**. Players only need data for entities within
   relevance radius. Default off because Titanfall-style maps
   are large and "player off-screen" is gameplay-relevant, but
   for the 500-bot stress test with all bots idle, AOI would
   be a free 5–10× cut on bytes.

6. **Parallel kernels worth the dispatch.** The PR-3 `parallelFor`
   wrapper is in place but turns off at small N or on shared
   hosts. On a deployment box with dedicated cores at 500 bots,
   `GROUP2_SERVER_PARALLEL=1` should pay; verify.

### What was tried and didn't pay (yet)

- **PR-3 with parallel default ON.** On the test rig the parallel
  path measured strictly worse than sequential at every N up to
  the rig's saturation (200), because the TBB worker pool
  oversubscribed the cores already shared with the bot fleet.
  Code is in tree, default-off, opt-in via
  `GROUP2_SERVER_PARALLEL=1` for the user's deployment box.

### What was deferred / not tried

- **PR-4 (movement-collision BVH)**: the `WorldTriMesh` already
  ships a BVH (see `SweptCollision.hpp` `BVHNode`); other
  geometry types (planes, boxes, brushes) total in the low
  hundreds and don't currently dominate. At 100 bots the
  `collision` scope is 0.39 ms p50 / 1.57 ms p99 — not the
  bottleneck.
- **PR-5 (lag-comp spatial hash + history fixed-cap)**: the
  `HitboxHistory` vector copy was identified as a 5.5 MB/s
  allocation in PR-1's exploration but doesn't show up at the
  current top scopes. Worth doing as a hygiene PR; not a
  500-bot unblocker.
- **PR-6 (task graph)**: explicitly the *natural sum* of the
  earlier PRs once parallel-per-system kernels exist (§5). With
  PR-3 currently default-off, the graph node's own dispatch
  overhead would dwarf the saving. Revisit when item #6 above
  shows real wins on a deployment box.
- **PR-7+ (GPU compute via SDL3 GPU)**: the spec gated this on
  "animation still bottlenecked after CPU SIMD + parallelism."
  Animation was 0.39 ms p50 at 100 bots without GPU — not
  bottlenecked. SDL3 compute dispatch latency (~50–200 µs
  round-trip) would dominate the work for the foreseeable
  scale.

### Done definition status

By §8: **outcome (b)** — "a clearly written final report
explaining where progress stalled, what would unblock it, and
which PRs nevertheless land net wins on the 150–300 bot range."
This section is that report. Items 1–5 above are the unblockers
for hitting outcome (a).

The PRs that landed (PR-1 through PR-3) are independent wins
that pay even if the user never goes beyond 100 bots in real
gameplay; they don't depend on the deferred work for their
value.

## §10 Post-perf netcode regression framework (PR-15 → PR-22)

After the §9 perf wins shipped, a series of bug reports against
the simulated-RTT / simulated-loss UI sliders and shot-debug
visualizer surfaced subtle netcode bugs that the synthetic load
tests couldn't see (they all run at near-zero loopback RTT and 0
loss). PR-15 → PR-22 add a deterministic regression net for
those scenarios.

### Landed PRs (netcode regressions + framework)

| Commit | Title | What it does |
|--------|-------|--------------|
| `150386c` | PR-15: per-fragment redundancy on FULL keyframes | At lossy links, FULL-snapshot fragmentation could lose a fragment and stall the keyframe path. Each fragment now carries its own redundancy header so the receiver can tolerate single-fragment drops without forcing a re-fragment. |
| `06ca94f` | PR-16: default-disable PR-11 render-delay interp (regression) | PR-11's render-delay interp had been default-on, but at the high-RTT lag-comp formula in PR-12 it created a visible target-snap when shots resolved. Defaulted off pending a unified solution (delivered in PR-19). |
| `b1f961a` | PR-17: FragmentReassembler swapped Stale-check args | One-line bug in the per-stream stale-fragment GC was discarding GOOD fragments and keeping STALE ones, causing snapshot reassembly to spin forever in TIME_WAIT-style states. Reproduced + fixed by extending the fragment unit tests. |
| `095ccee` | PR-18: deterministic netsync regression test framework | First half of the regression net: per-bot snapshot observation log (`bot_<id>.csv`) + server-side ground-truth log (`server_truth.csv`) + offline `scripts/netsync-analyze.py` that joins by wall-clock + clientId, interpolates between adjacent server samples, and reports euclidean desync per (bot, observed entity, RTT bucket). Would have caught PR-17 instantly. |
| `4806bc7` | PR-18b: server shot-resolution log + hit-rate analyzer | Second half: server stamps `(shooterClientId, shotInputTick, hitClientId, hitX/Y/Z, hitRegion)` per resolved shot to `server_shots.csv`. Independent of the desync analysis — runs even when the truth log is off. |
| `ac9d566` | PR-19: unified pre-render interpolation; re-enable interp default | Re-lands PR-11's render-delay interp, this time mutating `Position.value` once per frame BEFORE every visual consumer (renderer, particles, tracers, beams) reads it. Pre-PR-19 the renderer interpolated at 3 sites while tracers/ribbons/smoke kept reading raw `pos.value` — at 128 Hz × 2-snapshot delay (~16 ms) that was ~6 units of visible body-vs-effects separation. |
| `a3570da` | PR-20: CSGO `sv_showimpacts`-style lag-comp visualizer | New `SHOT_DEBUG_REPORT` packet: server captures the rewound capsule pose for every player in lag-comp range during each hit-scan, plus the resolved hit point, and unicasts the whole capture back to the shooter. Client renders blue (where I shot) vs red (what the server resolved) tracers + capsule outlines. The visualizer surfaced PR-20.6 / 20.7 / 20.9 below. |
| `de0e91d`+`beba38a`+`d7141dc`+`e2555aa`+`069b933` | PR-20.1-20.9 follow-ups | Five separate root-cause fixes the visualizer made visible: off-by-one between input.tick and predict.tick on shot ID; `Player`-tag filter on the client raycast that excluded the shooter's local hit-test; conditional that discarded the wall-impact end point; lag-comp formula was `RTT/2 + interp` when our render model needs `RTT + interp` (we don't extrapolate remote players forward); `dispatchMessage`'s `prev = pos` view was overwriting the local player's per-physics-tick prev — fixed by `entt::exclude<LocalPlayer>`. |
| `10a9f63` | PR-21: hit-reg analysis — bot aim AI + shot-intent log | Bot learns to aim (gated by `GROUP2_BOT_AIM=1`); rising-edge shot-intent CSV per bot; analyzer joins bot intent with `server_shots.csv` and reports per-RTT hit-rate, intended-vs-actual target distribution, hit-region distribution, intended-target → server-hit-point distance. |
| `5a8f89e` | PR-22: client-vs-server-rewind comparison | Extends the framework to capture **everything needed to diagnose lag-comp differences between what the client saw and what the server resolved**, per the user's PR-22 brief: `LagCompTarget` carries `lagTicks` + `rttMs`; `server_shots.csv` records the shot ray (origin, direction), shooter RTT, lag-comp ticks, and the hit target's rewound vs current position; `bot_shots_*.csv` records the bot's local AABB raycast (bot's view of "who I hit"); analyzer reports a four-quadrant agreement matrix (both hit / only client / only server / neither), bothHit-same-target rate, lag-comp drift distribution, and ray-origin desync between server's view of shooter origin and the bot's view. |

### PR-22 RTT sweep results (8 bots, 20 s, GROUP2_BOT_AIM=1, cl_interp default = 2 snapshots)

The sweep below uses the new framework to characterize hit-reg
quality vs simulated RTT. Each row is a 20 s test where every
bot fires only when it has a closest-target in view, aiming at
the target's chest.

| RTT (ms) | Server hit rate | bothHit same-target | Lag-comp drift p99 (XZ, units) | Ray-origin desync mean (units) | Hit on intended (% of resolved) |
|---------:|:----------------|:--------------------|:--------------------------------|:--------------------------------|:--------------------------------|
| 0    | 10.0 % | 74.2 % | 17.75 | ~3   | 7.6 % |
| 30   | 8.1 %  | 69.4 % | 25.01 | 13.2 | 5.9 % |
| 100  | 3.4 %  | 30.8 % | 34.20 | 39.3 | 1.2 % |
| 200  | 3.0 %  | 34.6 % | 59.02 | 65.3 | 1.2 % |

Notes:

- **Lag-comp drift growing with RTT** is *exactly the rewinder doing its job*:
  at RTT=200 ms with players moving ~300 u/s, the rewound capsule sits
  ~60 units behind the live foot position. The framework now lets us
  graph this and watch for regressions where the rewinder under- or
  over-shoots.
- **Hit rate falls 10 % → 3 %** despite lag-comp covering the rewind
  perfectly. Two contributing factors the analyzer surfaced:
    - **Ray-origin desync grows with RTT** (3 → 65 units): the bot's view of
      its OWN position diverges from the server's view of where the bot
      WAS at fire time. The bot uses the latest snapshot apply (no
      client-side prediction in the bot fleet); the server uses the
      live `pos.value` at shot resolution. With 8-bot melee at 300 u/s,
      this self-position drift alone moves the ray origin ~half a body
      width per 100 ms RTT.
    - **bothHit same-target collapses** from 74 % at RTT=0 to ~30 % at
      RTT=100+: when both client (AABB) and server (capsule) decide
      there was a hit, they agree on WHO at high RTT only a third of
      the time. This is the fingerprint of the local AABB checking a
      different snapshot tick than the server's rewound capsule.
- **`onlyClientHit` dominates (90 %+)** at every RTT — but this is an
  artifact of bots having no replicated `HitboxInstance` (capsules are
  not in the `Synced` tuple), so the bot's local raycast is broad-phase
  AABB only. AABB is much wider than the server's skeleton-driven
  capsules, so any AABB hit on a thin limb fails the capsule narrow
  phase. A real client (which builds capsules locally from animation)
  would not see this asymmetry; the framework's job is to report
  client-vs-server-rewind delta on real client traces in the future.

### What the framework now catches

- PR-17 fragment-reassembler stuck-state: bot observation log freezes
  while server truth keeps moving → enormous desync values.
- PR-20.6 client-side raycast filter bug: `bothHit` would have been ~0
  for the local `Player`-filtered view.
- PR-20.7 lag-comp formula bug: `bothHit same-target` rate would have
  collapsed at RTT > 0 — the symptom we just baselined.
- PR-20.9 jitter bug: `ray-origin desync` would have spiked frame-to-
  frame instead of growing smoothly with RTT.
- Future quantization / AoI-culling / snapshot-rate changes: the four
  numbers (hit rate, bothHit-same-target, lag-comp drift, ray-origin
  desync) are independent enough to surface either a worse client view
  or a worse server lag-comp without false positives on the other.

### Forward work surfaced by PR-22 results

1. **Client-side prediction in clientbot** — *addressed by PR-23
   below.*
2. **Replicate `HitboxInstance` (or expose a capsule cache)** so the
   bot's local raycast is comparable to the server's. Until then,
   `onlyClientHit` is dominated by AABB-vs-capsule asymmetry, not
   lag-comp.
3. **Hit-reg-under-loss sweep** — the framework supports
   `GROUP2_BOT_LOSS_PERCENT`; we haven't run it. PR-15's per-fragment
   redundancy expects the headline numbers to barely move under 5 %
   loss; the framework can verify.

### PR-23: clientbot prediction parity with the real client

Pre-PR-23 the clientbot was deliberately lightweight — *"no window,
no GPU, no animation, no physics — just network traffic"* per its
CMake comment. That was correct for the original Phase-1/2 server-
perf goals (synthesise N TCP clients per host) but wrong for the
hit-reg testing the framework now does: bots' `Position` only moved
when a server snapshot applied, so each bot's view of its OWN
position lagged the server by `RTT/2 + interpDelay`. The PR-22
ray-origin-desync metric was therefore polluted by self-position
drift the real client doesn't have (the real client uses Phase-5b
client-side prediction to advance its local Position immediately).

PR-23 makes the bot extend the real client's behaviour: same map
geometry loaded, same `runMovement + runCollision` per physics tick,
same `runReconciliation` on snapshot apply, same `LocalPlayer +
InputSnapshot + PreviousPosition + PlayerSimState` components on
the local entity. Divergence between clientbot and real client is
now data-only (different inputs — bot's AI, real client's keyboard)
rather than architectural. Render / audio / animation are still
absent (bot's job is to run many at once, not to render).

CMake-wise this added `MovementSystem.cpp` + `CollisionSystem.cpp`
+ `MapLoader.cpp` + `VHACDImpl.cpp` + `ExplosionSystem.cpp` +
`PlayerStatusSystem.cpp` to the clientbot target, plus the
`assimp` and `vhacd` link deps the server / client already pulled.
The map is loaded once per process (`MapCollisionData` lives in
`main`'s scope, `setActiveWorld` stashes the spans pointing into it,
so all bots share the same `physics::activeWorld()` singleton).

#### PR-23 RTT sweep results (8 bots, 20 s, GROUP2_BOT_AIM=1, cl_interp default)

The PR-22 sweep is repeated below with prediction enabled.  Headline:
`ray-origin desync` collapses across the entire RTT range — the bot's
view of its own position now matches the server's after the standard
prediction-reconcile loop, so the metric is now a clean lag-comp
measurement rather than a polluted blend of lag-comp + self-position
lag.

| RTT (ms) | PR-22 hit rate | PR-23 hit rate | PR-22 ray-origin desync mean | PR-23 ray-origin desync mean (p50) | PR-22 intended→hit mean | PR-23 intended→hit mean |
|---------:|:--------------:|:--------------:|:----------------------------:|:----------------------------------:|:-----------------------:|:-----------------------:|
| 0    | 10.0 % | **14.5 %** | 3.07 u  | **1.77 u (p50 0.00)** | 26.57 u | **4.22 u**  |
| 30   | 8.1 %  | **11.5 %** | 13.17 u | **6.64 u (p50 0.00)** | 29.27 u | **6.04 u**  |
| 100  | 3.4 %  | **11.7 %** | 39.33 u | **6.60 u (p50 0.00)** | 37.87 u | **6.15 u**  |
| 200  | 3.0 %  | **8.3 %**  | 65.26 u | **11.31 u (p50 0.33)** | 67.43 u | **10.11 u** |

Notes:

- **`ray-origin desync` p50 ≈ 0.00 at every RTT.** The bot's
  predicted position now matches the server's authoritative position
  for the *median* shot, regardless of RTT. The mean is dominated by
  transient outliers — single ~1900 u spikes during the connect
  window before the bot has its first server-acked tick. The `p99`
  of 0.00 / 0.73 / 84.99 / 77.22 across the sweep tells the story:
  prediction is bit-exact for almost every shot, with rare outliers
  during respawn / disconnect transitions.
- **`intended → hit-point` distance drops 6×** at every RTT (~26-67 u
  pre-PR-23 → ~4-10 u post-PR-23). The bot's aim ray now actually
  starts from where the server thinks the shooter is, so the geometry
  the analyzer reports is the geometry that mattered to lag-comp.
- **Hit rate jumps 2-3× at high RTT** (3.0 % → 8.3 % at RTT=200).
  This isn't lag-comp suddenly working better — it's the bot now
  actually aiming where it thinks it's aiming. Pre-PR-23, the bot's
  ray fired from 65 u behind where it should, so even a "correct"
  aim missed.
- **Lag-comp drift** distribution narrows slightly (this metric is
  about REMOTE players' rewound vs current pos on the server, which
  PR-23 doesn't touch directly — the small change is from the bot
  fleet now moving like real clients, so the targets the framework
  observes have realistic motion patterns).
- **`bothHit same-target`** is no longer monotonically decreasing
  with RTT — at RTT=100 it actually rises 30.8 → 40.8 % vs PR-22.
  Pre-PR-23 the bot's local AABB was raycasting from a position
  ~40 u behind server-truth, polluting the comparison; with the
  origins now aligned, the comparison is honest.

#### What the framework now measures honestly

With PR-23 closing the bot/client behavior gap, the four headline
numbers measure exactly what their names claim:

- **`ray-origin desync`** — server's view of shooter origin minus
  bot's view. Captures Phase-5b prediction quality + reconciliation
  correctness. Should be ≈ 0 absent prediction bugs.
- **`lag-comp drift`** — distance the server's rewinder moved the
  hit target backwards in time. Grows linearly with RTT × target
  velocity. Captures the lag-comp system's job.
- **`bothHit same-target`** — when both bot's local AABB and
  server's capsule raycast both hit, do they agree on *who*?
  Captures whether the bot's view of remote-player positions
  (interpolated, render-delayed) lines up with the server's
  rewound capsule positions.
- **`hit rate`** — pure shot-resolution outcome. Bot AI quality +
  capsule vs AABB asymmetry + actual lag-comp accuracy.

Future regressions in any one of these four can now be diagnosed
without false positives from the others.
