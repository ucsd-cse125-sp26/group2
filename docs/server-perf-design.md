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
