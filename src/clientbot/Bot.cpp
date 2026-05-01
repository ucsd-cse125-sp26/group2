/// @file Bot.cpp
/// @brief Implementation of the headless client bot.

#include "Bot.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include <pthread.h>
#include <sched.h>
#endif

#include <cmath>
#include <cstdint>
#include <random>

Bot::~Bot()
{
    if (thread_.joinable())
        thread_.join();
    if (initialized_)
        client_.shutdown();
}

bool Bot::init(const std::string& host, Uint16 port, int botId)
{
    botId_ = botId;
    if (!client_.init(host.c_str(), port)) {
        SDL_Log("[bot %d] connection failed", botId_);
        return false;
    }

    // The real Client wires several gameplay callbacks. The bot doesn't act
    // on game events — only the bandwidth and frame timing matter for
    // network load-testing — so leave the callback slots empty. Client::poll
    // will still happily decode and apply snapshots into registry_.
    initialized_ = true;
    SDL_Log("[bot %d] connected to %s:%u", botId_, host.c_str(), port);
    return true;
}

void Bot::setSimulatedLatencyMs(int totalMs)
{
    client_.setSimulatedLatencyMs(totalMs);
}

void Bot::setSimulatedLossPercent(int percent)
{
    client_.setSimulatedLossPercent(percent);
}

void Bot::start(const std::atomic<bool>& stopFlag)
{
    if (!initialized_) {
        SDL_Log("[bot %d] start() called without successful init()", botId_);
        return;
    }
    thread_ = std::thread([this, &stopFlag] { runLoop(stopFlag); });

    // PR-9 (server-perf): optional CPU affinity pinning for bot
    // threads. With 500 bot threads on a 16-core box and no
    // affinity, the kernel migrates threads across cores
    // constantly — each migration cold-pages caches and the server
    // pays for the bot fleet's cache thrash.
    //
    // GROUP2_BOT_CPUS=lo[,hi]  pins every bot thread's affinity
    // mask to cores [lo..hi] (inclusive). Common patterns:
    //   GROUP2_BOT_CPUS=8,15    bots on cores 8-15, server free
    //                            on cores 0-7 (16-core host)
    //   GROUP2_BOT_CPUS=12,15   bots on the last 4 cores only
    //                            (most aggressive — assumes server
    //                            uses 0-11)
    //
    // Linux-only (pthread_setaffinity_np). On macOS / Windows the
    // env var is silently ignored.
#if defined(__linux__)
    if (const char* cpus = std::getenv("GROUP2_BOT_CPUS")) {
        int lo = -1;
        int hi = -1;
        const char* comma = std::strchr(cpus, ',');
        char* end = nullptr;
        const long parsedLo = std::strtol(cpus, &end, 10);
        if (end != cpus) {
            lo = static_cast<int>(parsedLo);
            if (comma != nullptr) {
                const long parsedHi = std::strtol(comma + 1, &end, 10);
                if (end != comma + 1)
                    hi = static_cast<int>(parsedHi);
            } else {
                hi = lo;
            }
        }
        if (lo >= 0 && hi >= lo) {
            cpu_set_t mask;
            CPU_ZERO(&mask);
            for (int c = lo; c <= hi; ++c) {
                CPU_SET(c, &mask);
            }
            const int rc = pthread_setaffinity_np(thread_.native_handle(), sizeof(mask), &mask);
            if (rc != 0 && botId_ == 0) {
                // Log once (only on bot 0) so the user sees the
                // problem if pinning fails. Subsequent bots are
                // assumed to fail for the same reason.
                SDL_Log("[clientbot] WARNING: pthread_setaffinity_np failed (rc=%d) for cores %d-%d", rc, lo, hi);
            }
        } else if (botId_ == 0) {
            SDL_Log("[clientbot] WARNING: GROUP2_BOT_CPUS='%s' malformed (expected 'lo,hi' or 'lo')", cpus);
        }
    }
#endif
}

void Bot::join()
{
    if (thread_.joinable())
        thread_.join();
}

float Bot::getCurrentRttMs() const
{
    // The bot's worker thread is the sole writer to NetworkStats.avgRttMs
    // (via Client::poll → updateRttFromPong). Reading from another thread
    // is non-atomic on float, but the value is monotonic-smoothed and we
    // only consume it for human-readable aggregates — torn reads have
    // negligible practical impact on a p99/p50 estimate over 100s of bots.
    return client_.getNetStats().avgRttMs;
}

void Bot::runLoop(const std::atomic<bool>& stopFlag)
{
    // PR-4 (server-perf): allow the bot's tick rate to be reduced for
    // stress-test runs. Each bot is a thread that spin-waits the
    // sub-millisecond fraction of its tick boundary; at 200+ bots on
    // a 16-core host the spinning fleet alone exhausts the machine's
    // CPU regardless of how cheap the server is. Lowering the bot
    // tick rate from 128 Hz to e.g. 32 Hz cuts bot-side CPU ~4× —
    // each bot still exercises the server's full input-redundancy +
    // PING/PONG path, just with proportionally fewer
    // `sendInputSnapshot` calls per second.
    //
    // Production gameplay should keep 128 Hz for fidelity; the env
    // override is a stress-test tool. Default unchanged.
    int tickHz = k_tickHz;
    if (const char* p = std::getenv("GROUP2_BOT_TICK_HZ")) {
        char* end = nullptr;
        const long n = std::strtol(p, &end, 10);
        if (*end == '\0' && n >= 8 && n <= 256)
            tickHz = static_cast<int>(n);
    }

    // GROUP2_BOT_AI=1 turns each bot into a tiny stochastic agent: it walks
    // in random directions, jumps, sprints, rotates the look angle, and pulses
    // shooting on top of the existing fire cadence.  Default OFF so existing
    // network/server load tests still see the deterministic idle state.  When
    // ON, the perf bench measures something much closer to real in-game FPS:
    // the server is doing real movement physics + lag-compensated hits, and
    // the real client renders chars that are actually animating + pose-changing
    // every frame.
    const bool aiEnabled = []() {
        const char* p = std::getenv("GROUP2_BOT_AI");
        return p != nullptr && p[0] != '\0' && p[0] != '0';
    }();

    // Per-bot RNG.  Seed mixes botId + a clock-derived 32-bit so two adjacent
    // bots don't desynchronise lock-step and produce identical input streams.
    std::mt19937 rng{static_cast<uint32_t>(botId_) ^ static_cast<uint32_t>(SDL_GetPerformanceCounter() & 0xFFFFFFFFu)};
    std::uniform_real_distribution<float> uni01{0.0f, 1.0f};
    // Per-bot orientation drift. yaw drifts through 2π over ~8s, pitch sweeps
    // shallow up/down. Each bot picks an independent yaw rate and phase so the
    // fleet doesn't all face the same way.
    const float yawRate = 0.5f + uni01(rng) * 1.5f; // 0.5–2.0 rad/s
    const float pitchRate = 0.3f + uni01(rng) * 0.8f;
    const float yawPhase = uni01(rng) * 6.2832f;
    const float pitchPhase = uni01(rng) * 6.2832f;
    // Movement state machine: the bot picks a "direction key" combo and holds
    // it for ~0.5–1.5s before re-rolling.  Mirrors how a human plays.
    int moveCombo = 0; // bit 0=fwd, 1=back, 2=left, 3=right
    int sprintTicksRemaining = 0;
    int jumpCooldownTicks = 0;
    int comboTicksRemaining = 0;
    auto rerollCombo = [&]() {
        // ~70% chance pure forward direction; 30% strafe or backpedal.
        const float r = uni01(rng);
        if (r < 0.55f)
            moveCombo = 0b0001; // forward
        else if (r < 0.75f)
            moveCombo = 0b0101; // forward+left
        else if (r < 0.90f)
            moveCombo = 0b1001; // forward+right
        else if (r < 0.95f)
            moveCombo = 0b0010; // back
        else
            moveCombo = 0;      // stand still
        comboTicksRemaining =
            static_cast<int>(0.5f * static_cast<float>(tickHz) + uni01(rng) * 1.0f * static_cast<float>(tickHz));
        if (uni01(rng) < 0.30f)
            sprintTicksRemaining = static_cast<int>(0.4f * static_cast<float>(tickHz));
    };
    if (aiEnabled)
        rerollCombo();

    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 tickDurationCounters = perfFreq / static_cast<Uint64>(tickHz);
    Uint64 nextTick = SDL_GetPerformanceCounter() + tickDurationCounters;

    while (!stopFlag.load(std::memory_order_relaxed)) {
        // ── Input: stamp tick onto a near-idle InputSnapshot and send. ──
        //
        // We don't need realistic inputs for load testing — the goal is to
        // exercise the same network path a real client uses (multi-input
        // redundancy + tick-based dedup on the server). All-zero movement
        // means the server applies idle state every tick; the wire traffic
        // and per-tick processing cost is identical to a real connected
        // player standing still.
        //
        // Phase 6: pulse `shooting=true` for a burst of consecutive
        // ticks. Single-tick pulses get shadowed by the next tick's
        // shooting=false inside the 5-input redundancy ring (the
        // server applies inputs in order from each packet, so the
        // last one in the packet wins for the entity's InputSnapshot
        // that runWeapon sees). 32-tick bursts (~250 ms) ensure
        // multiple subsequent packets carry shooting=true as the
        // newest snapshot, so handleFire actually runs and exercises
        // the lag-compensation path.
        //
        // Cadence: 32 ticks ON (~250 ms) every 256 ticks (~2 s). With
        // typical fire cooldowns of 50–200 ms that gets ~1–4 shots
        // per cycle per bot — enough activity to exercise the rewind
        // path under bot-only tests, sparse enough not to saturate
        // the kill feed.
        ++predictTick_;
        input_.shooting = (predictTick_ % 256u) < 32u;
        input_.tick = predictTick_;

        if (aiEnabled) {
            // Continuous yaw/pitch sweeps: rotation rate is per-bot but the
            // sweep is bounded so the bot doesn't spin uncontrollably.
            const float t = static_cast<float>(predictTick_) / static_cast<float>(tickHz);
            input_.yaw = yawPhase + t * yawRate;
            input_.pitch = 0.2f * std::sin(pitchPhase + t * pitchRate); // shallow up/down

            // Movement combo: re-roll every comboTicksRemaining ticks.
            if (--comboTicksRemaining <= 0)
                rerollCombo();
            input_.forward = (moveCombo & 0b0001) != 0;
            input_.back = (moveCombo & 0b0010) != 0;
            input_.left = (moveCombo & 0b0100) != 0;
            input_.right = (moveCombo & 0b1000) != 0;

            // Sprint while the sprint timer is running.
            input_.sprint = sprintTicksRemaining > 0;
            if (sprintTicksRemaining > 0)
                --sprintTicksRemaining;

            // Jump occasionally — pulse for one tick, then rate-limit so the
            // server's lag-comp + ground check both get exercised.
            if (jumpCooldownTicks > 0) {
                input_.jump = false;
                --jumpCooldownTicks;
            } else if (uni01(rng) < 0.01f) {    // ~1.3 jumps/sec at 128 Hz
                input_.jump = true;
                jumpCooldownTicks = tickHz / 2; // 0.5s lockout
            } else {
                input_.jump = false;
            }

            // Override the deterministic shooting cadence: pulse with a much
            // higher duty cycle so the lag-comp + bullet-VFX path runs hot.
            input_.shooting = uni01(rng) < 0.10f;
        }

        if (!client_.sendInputSnapshot(input_)) {
            SDL_Log("[bot %d] sendInputSnapshot failed; bailing", botId_);
            break;
        }

        // ── Drain inbound: snapshots, particle/kill events, PONG. ────────
        //
        // Crucial — without this the server's TCP send buffer would fill,
        // back-pressuring server flushSend and corrupting the load-test
        // signal we're trying to measure. Client::poll already implements
        // the Phase-1 drain-fully fix in MessageStream.
        if (!client_.poll(registry_)) {
            SDL_Log("[bot %d] server connection died", botId_);
            break;
        }
        // PR-1 (server-perf): mark the bot ready for fleet-RTT sampling
        // after the first successful tick. RTT is still 0 here until
        // the first PONG arrives, but the fleet aggregator filters
        // bots with `getCurrentRttMs() > 0` so a brief warmup window
        // is acceptable.
        if (!ready_.load(std::memory_order_relaxed))
            ready_.store(true, std::memory_order_relaxed);

        // ── Pacing: hybrid sleep + spin to hit the next tick boundary. ──
        //
        // PR-4 (server-perf): when `GROUP2_BOT_NO_SPIN=1` is set, drop
        // the sub-millisecond spin and rely on `SDL_Delay` alone. With
        // 200+ bots on a single host, the cumulative spinning was
        // exhausting the host's CPU and producing artificially-high
        // RTT measurements at the bot fleet (the bots simply weren't
        // running often enough). The trade-off: ±1 ms tick-cadence
        // jitter per bot, fine for load-test purposes, unsuitable for
        // an actual gameplay client.
        static const bool k_noSpin = []() {
            const char* p = std::getenv("GROUP2_BOT_NO_SPIN");
            return p != nullptr && p[0] != '\0' && p[0] != '0';
        }();

        const Uint64 now = SDL_GetPerformanceCounter();
        if (now < nextTick) {
            const Sint64 sleepMs = static_cast<Sint64>((nextTick - now) * 1000 / perfFreq) - 1;
            if (sleepMs > 0)
                SDL_Delay(static_cast<Uint32>(sleepMs));
            if (!k_noSpin) {
                while (SDL_GetPerformanceCounter() < nextTick && !stopFlag.load(std::memory_order_relaxed)) {
                    // spin
                }
            }
        }
        nextTick += tickDurationCounters;

        // ── Periodic stats log: every 1024 ticks (~8 s @ 128 Hz). ────────
        if ((predictTick_ & 0x3FF) == 0) {
            const auto& s = client_.getNetStats();
            SDL_Log("[bot %d] tick=%u rtt=%.1fms recv=%.1fKB/s send=%.1fKB/s",
                    botId_,
                    predictTick_,
                    static_cast<double>(s.avgRttMs),
                    static_cast<double>(s.recvBytesPerSec / 1024.0f),
                    static_cast<double>(s.sendBytesPerSec / 1024.0f));
        }

        // Refresh bandwidth EMA (the Client expects ~per-frame calls).
        client_.updateStats(1.0f / static_cast<float>(tickHz));

        // Send a PING every 128 ticks (~1 s @ 128 Hz). The Client decodes
        // PONG responses and updates avgRttMs in its NetworkStats, so the
        // periodic stats log above will surface RTT growth as the server
        // gets loaded — the headline metric for "is the network keeping up
        // at 50 bots? 100?".
        if ((predictTick_ % 128u) == 0u) {
            client_.sendPing();
        }
    }

    SDL_Log("[bot %d] loop exit (stop=%d, tick=%u)", botId_, stopFlag.load() ? 1 : 0, predictTick_);
}
