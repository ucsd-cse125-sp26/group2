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
