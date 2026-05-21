# Client perf toggles — A/B reference

Every knob below is set via env var on the client process.  Combine freely.
The bench harness (`scripts/perf-100bots.sh`) inherits the calling shell's env,
so prefixing the script call with `KEY=VALUE …` flips a toggle for one run.

## Headline knobs (real perf impact)

| Env var | Default | Effect |
|---|---|---|
| `BENCH_SECONDS=N` | unset | Runs for N seconds, prints submitted-frame profile + p1/p5/p99 fps, exits. Forces uncapped mailbox present by default and disables ImGui submit (see `imguiEnabled` flag). |
| `GROUP2_BOT_AI=1` | off | Each bot becomes a stochastic agent (random walk, aim sweep, jump, fire).  The "in-game" workload — without it bots stand idle. |
| `GROUP2_NO_IMGUI=1` | off | Skips ImGui prepare + render-data submission.  Bench mode flips this on automatically.  +19 % median fps in single-run direct comparison. |
| `GROUP2_CLIENT_CORES="0,1,2,3"` | unset | `pthread_setaffinity_np` on the main render thread to those CPU cores (Linux only).  Stops the kernel migrating us onto cores busy with server / bot threads. |
| `GROUP2_WORKERS=N` | hardware_concurrency()/2 (clamped to 7) | Worker count for the parallel-for thread pool. 0 disables. |
| `GROUP2_PARALLEL_THRESHOLD=N` | 256 | Min item count for parallel-for to actually fan out (below threshold runs inline to avoid wake-up overhead exceeding the work).  Drop to ~16 to verify the parallel path runs on small workloads (will be slower at small counts). |
| `BENCH_RENDER_SCALE=F` | 1.0 | HDR + post-process internal resolution multiplier (0.05–2.0).  At 0.5, fragment-shader work in HDR pass + GTAO + bloom + SSR + volumetrics + TAA + CAS drops ~4x.  Tonemap upscales bilinearly to swapchain res for free.  Sweet spot 0.5–0.75 on fragment-bound scenarios; below 0.25 hurts due to swap-queue pressure. |

## Diagnostic knobs (bisect post-process cost)

| Env var | Effect |
|---|---|
| `BENCH_NO_POSTFX=1` | Disable bloom + SSR + GTAO + volumetrics + AA at the tonemap composite step. |
| `BENCH_NO_SHADOWS=1` | Disable cascaded shadow map pass. |
| `BENCH_NO_TAA=1` | Disable TAA + motion-vectors. |
| `BENCH_NO_BLOOM=1` | Disable bloom only. |
| `BENCH_NO_SSR=1` | Disable SSR only. |
| `BENCH_NO_GTAO=1` | Disable SSAO/GTAO only. |
| `BENCH_NO_VOL=1` | Disable volumetric lighting only. |
| `BENCH_PRESENT=immediate\|mailbox\|vsync` | Override swapchain present mode for stutter analysis.  MAILBOX is correct for normal play (CPU/GPU pipeline parallelism); IMMEDIATE removes the swap queue (good for inspecting the *true* per-frame GPU time but ~30× slower median fps because CPU/GPU run serialised). |

## Bot-side knobs

| Env var | Effect |
|---|---|
| `GROUP2_BOT_TICK_HZ=N` | Bot tick rate (default 128).  Drop to e.g. 32 to lighten the colocated-host load when running 200+ bots. |
| `GROUP2_BOT_NO_SPIN=1` | Disable the bot's sub-millisecond spin-wait between ticks (each bot is a thread; spinning saturates host cores when many bots run). |

## Recipes

```bash
# Default bench: closest to in-game perf measurement.
GROUP2_BOT_AI=1 bash scripts/perf-100bots.sh 100 25

# Maximum fps push — combine all knobs that help:
GROUP2_BOT_AI=1 GROUP2_NO_IMGUI=1 GROUP2_CLIENT_CORES=8,9,10,11,12,13,14,15 \
    BENCH_RENDER_SCALE=0.75 \
    bash scripts/perf-100bots.sh 100 25

# Bisect: which post-process pass costs the most?
for v in NO_POSTFX NO_BLOOM NO_SSR NO_GTAO NO_VOL NO_TAA; do
    GROUP2_BOT_AI=1 BENCH_$v=1 bash scripts/perf-100bots.sh 100 20
done

# Stress test the parallel pool (needs >=256 candidates to activate):
GROUP2_BOT_AI=1 GROUP2_PARALLEL_THRESHOLD=16 bash scripts/perf-100bots.sh 200 25

# Confirm the median is GPU-bound vs CPU-bound:
GROUP2_BOT_AI=1 BENCH_PRESENT=immediate bash scripts/perf-100bots.sh 100 25
# (median tanking ~30× when IMMEDIATE flips means MAILBOX was successfully
#  pipelining CPU + GPU; not a problem.)
```

## Bench output cheat sheet

```
[bench] elapsed=25.0s samples=N avg=A median=M p5=P5 p1=P1 min=Mn max=Mx
[bench] median-band  total=Tms phys=… net=… anim=… part=… ent=… ui=… draw=…
[bench] slowest-1%   total=Tms phys=… net=… anim=… part=… ent=… ui=… draw=…
[bench] top-5 slowest individual frames:
[bench]   total=Tms phys=… net=… anim=… … draw=… (acq=… record=… sub=…)
```

* `median-band` = average of frames near the 50 %-ile; the typical-frame cost.
* `slowest-1%` = average of the 1 % slowest frames; what the p1 tail is made of.
* `acq` = `SDL_AcquireGPUSwapchainTexture` blocking time (swap-queue back-pressure).
* `record` = CPU time recording the GPU command buffer.
* `sub` = `SDL_SubmitGPUCommandBuffer` blocking time (rare with MAILBOX).
* If `acq` dominates the slow tail, you're GPU-saturated waiting on a swap image.
* If `rec` dominates, you're CPU-bound recording.
* If `sub` dominates, the driver is throttling submission (rare — usually means
  IMMEDIATE present mode forcing CPU/GPU serialisation).
