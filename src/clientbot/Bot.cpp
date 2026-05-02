/// @file Bot.cpp
/// @brief Implementation of the headless client bot.

#include "Bot.hpp"

#include "ecs/components/ClientId.hpp"
#include "ecs/components/CollisionShape.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/components/Position.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glm/geometric.hpp>
#include <limits>
#if defined(__linux__) || defined(__APPLE__) || defined(__unix__)
#include <pthread.h>
#include <sched.h>
#endif

#include <cmath>
#include <cstdint>
#include <random>
#include <string>

Bot::~Bot()
{
    if (thread_.joinable())
        thread_.join();
    if (initialized_)
        client_.shutdown();
    closeObservationLog();
    closeShotsLog();
}

// ── PR-18: per-bot snapshot observation log ─────────────────────────────

void Bot::openObservationLog()
{
    const char* prefix = std::getenv("GROUP2_BOT_OBS_CSV_PREFIX");
    if (prefix == nullptr || prefix[0] == '\0')
        return;

    // Path = `<prefix><botId>.csv`.  Bots run on independent threads,
    // each with its own file, so no inter-thread coordination needed.
    std::string path = std::string(prefix) + std::to_string(botId_) + ".csv";
    obsCsv_ = std::fopen(path.c_str(), "w");
    if (obsCsv_ == nullptr) {
        SDL_Log("[bot %d] PR-18: failed to open observation log at %s", botId_, path.c_str());
        return;
    }
    std::fprintf(obsCsv_, "wallTimeNs,observerBotId,observedClientId,posX,posY,posZ\n");
    std::fflush(obsCsv_);
    SDL_Log("[bot %d] PR-18: writing observation log to %s", botId_, path.c_str());
}

void Bot::writeObservationLog()
{
    if (obsCsv_ == nullptr)
        return;

    // Wall-clock at log time (after `client_.poll()` returned).  Used by
    // the offline analyzer to align with the server-side ground-truth
    // log via linear interpolation between adjacent server samples.
    const Uint64 nowNs = SDL_GetTicksNS();

    // Walk every replicated player entity (those carry ClientId).  This
    // intentionally skips projectiles, weapon spawners, etc — the
    // analyzer only cares about player desync for now.  Adding more
    // categories is one-line per category.
    auto view = registry_.view<const Position, const ClientId>();
    for (const auto e : view) {
        const auto& pos = view.get<const Position>(e);
        const auto& cid = view.get<const ClientId>(e);
        std::fprintf(obsCsv_,
                     "%llu,%d,%u,%.4f,%.4f,%.4f\n",
                     static_cast<unsigned long long>(nowNs),
                     botId_,
                     static_cast<unsigned>(cid.value),
                     static_cast<double>(pos.value.x),
                     static_cast<double>(pos.value.y),
                     static_cast<double>(pos.value.z));
    }
    // Flush every snapshot — small per-bot file, predictable on crash.
    // At 32-128 Hz × ~25 entities per snapshot × ~50 bytes/row this is
    // ~150 KB/s/bot, well within disk budget on any modern test rig.
    std::fflush(obsCsv_);
}

void Bot::closeObservationLog() noexcept
{
    if (obsCsv_ != nullptr) {
        std::fclose(obsCsv_);
        obsCsv_ = nullptr;
    }
}

// ── PR-21: bot-side shot-intent log ─────────────────────────────────────

void Bot::openShotsLog()
{
    const char* prefix = std::getenv("GROUP2_BOT_SHOTS_CSV_PREFIX");
    if (prefix == nullptr || prefix[0] == '\0')
        return;
    std::string path = std::string(prefix) + std::to_string(botId_) + ".csv";
    shotsCsv_ = std::fopen(path.c_str(), "w");
    if (shotsCsv_ == nullptr) {
        SDL_Log("[bot %d] PR-21: failed to open shots log at %s", botId_, path.c_str());
        return;
    }
    // PR-22: schema grew to include the bot's local AABB raycast
    // result.  Old runs without these columns are still loadable by
    // the analyzer (DictReader tolerates missing keys).
    std::fprintf(shotsCsv_,
                 "wallTimeNs,shooterClientId,shotInputTick,"
                 "originX,originY,originZ,"
                 "dirX,dirY,dirZ,"
                 "intendedTargetClientId,intendedTargetX,intendedTargetY,intendedTargetZ,intendedTargetDist,"
                 "botRayHit,botHitClientId,botHitX,botHitY,botHitZ,botHitDist\n");
    std::fflush(shotsCsv_);
    SDL_Log("[bot %d] PR-21: writing shot-intent log to %s", botId_, path.c_str());
}

void Bot::writeShotIntent(std::uint16_t shooterClientId,
                          std::uint32_t shotInputTick,
                          const glm::vec3& origin,
                          const glm::vec3& direction,
                          std::uint16_t intendedTargetClientId,
                          const glm::vec3& intendedTargetPos,
                          float intendedTargetDist,
                          bool botRayHit,
                          std::uint16_t botHitClientId,
                          const glm::vec3& botHitPos,
                          float botHitDist)
{
    if (shotsCsv_ == nullptr)
        return;
    const Uint64 nowNs = SDL_GetTicksNS();
    std::fprintf(shotsCsv_,
                 "%llu,%u,%u,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%u,%.4f,%.4f,%.4f,%.4f,"
                 "%d,%u,%.4f,%.4f,%.4f,%.4f\n",
                 static_cast<unsigned long long>(nowNs),
                 static_cast<unsigned>(shooterClientId),
                 static_cast<unsigned>(shotInputTick),
                 static_cast<double>(origin.x),
                 static_cast<double>(origin.y),
                 static_cast<double>(origin.z),
                 static_cast<double>(direction.x),
                 static_cast<double>(direction.y),
                 static_cast<double>(direction.z),
                 static_cast<unsigned>(intendedTargetClientId),
                 static_cast<double>(intendedTargetPos.x),
                 static_cast<double>(intendedTargetPos.y),
                 static_cast<double>(intendedTargetPos.z),
                 static_cast<double>(intendedTargetDist),
                 botRayHit ? 1 : 0,
                 static_cast<unsigned>(botHitClientId),
                 static_cast<double>(botHitPos.x),
                 static_cast<double>(botHitPos.y),
                 static_cast<double>(botHitPos.z),
                 static_cast<double>(botHitDist));
    std::fflush(shotsCsv_);
}

void Bot::closeShotsLog() noexcept
{
    if (shotsCsv_ != nullptr) {
        std::fclose(shotsCsv_);
        shotsCsv_ = nullptr;
    }
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

    // PR-18: open the per-bot observation log if requested.  Done here
    // (post-Client::init) so the bot's connection state is stable
    // before we start accumulating snapshot rows.
    openObservationLog();
    // PR-21: open the per-bot shot-intent log if requested.
    openShotsLog();

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

            // PR-21: aim-at-closest-target.  When `GROUP2_BOT_AIM=1`, the
            // bot replaces the random yaw/pitch sweep with a real aim
            // vector toward the closest visible remote player's centre
            // mass.  Combined with the existing 10 % shooting roll, this
            // gives an order-of-magnitude better hit rate than random
            // aim — necessary for the netsync framework's hit-reg
            // analysis at varying simulated RTT to produce meaningful
            // signal.  Toggle separately from `GROUP2_BOT_AI` so the
            // existing load-test benchmarks see the same random-aim
            // behaviour.
            static const bool k_aimEnabled = []() {
                const char* p = std::getenv("GROUP2_BOT_AIM");
                return p != nullptr && p[0] != '\0' && p[0] != '0';
            }();
            if (k_aimEnabled) {
                if (auto localEnt = client_.getLocalPlayerEntity(); localEnt && registry_.valid(*localEnt)) {
                    const auto* myPos = registry_.try_get<Position>(*localEnt);
                    const auto* myShape = registry_.try_get<CollisionShape>(*localEnt);
                    if (myPos != nullptr && myShape != nullptr) {
                        glm::vec3 closestPos{0.0f};
                        float closestHalfH = 36.0f;
                        float closestDist = 5000.0f;
                        bool foundTarget = false;
                        auto rview = registry_.view<Position, CollisionShape, ClientId>();
                        for (const auto e : rview) {
                            if (e == *localEnt)
                                continue;
                            const auto& tpos = rview.get<Position>(e);
                            const float d = glm::length(tpos.value - myPos->value);
                            if (d < closestDist) {
                                closestDist = d;
                                closestPos = tpos.value;
                                closestHalfH = rview.get<CollisionShape>(e).halfExtents.y;
                                foundTarget = true;
                            }
                        }
                        if (foundTarget) {
                            // PR-22: aim at the target's chest = foot
                            // position + halfExtents.y (one half-height
                            // above feet ≈ middle of the capsule).
                            // Earlier this was a hardcoded `+50` which
                            // assumed halfExtents.y ≈ 67 — way off the
                            // actual default 36, putting the aim point
                            // ~14 units above the head.  Fixed offset
                            // → near-perfect aim at RTT=0.
                            const glm::vec3 aimPoint = closestPos + glm::vec3{0.0f, closestHalfH, 0.0f};
                            const glm::vec3 eye = myPos->value + glm::vec3{0.0f, myShape->halfExtents.y * 0.75f, 0.0f};
                            const glm::vec3 to = aimPoint - eye;
                            const float horiz = std::sqrt(to.x * to.x + to.z * to.z);
                            input_.yaw = std::atan2(to.x, to.z);
                            input_.pitch = -std::atan2(to.y, std::max(horiz, 0.001f));
                        }
                    }
                }
            }
        }

        // PR-21: log shot intent on the rising edge of input.shooting.
        // Logged AFTER all aim/AI updates so the recorded yaw/pitch /
        // origin reflect the values that actually go on the wire this
        // tick.  Ordering matches the `(shooterClientId, shotInputTick)`
        // join key used by the analyzer (server stamps the same tick on
        // its side via `perf::shotlog::recordShotResolution`).
        {
            const bool wasShooting = prevShootingForLog_;
            const bool nowShooting = input_.shooting;
            prevShootingForLog_ = nowShooting;
            if (nowShooting && !wasShooting && shotsCsv_ != nullptr) {
                if (auto localEnt = client_.getLocalPlayerEntity(); localEnt && registry_.valid(*localEnt)) {
                    const auto* myCid = registry_.try_get<ClientId>(*localEnt);
                    const auto* myPos = registry_.try_get<Position>(*localEnt);
                    const auto* myShape = registry_.try_get<CollisionShape>(*localEnt);
                    if (myCid != nullptr && myPos != nullptr && myShape != nullptr) {
                        // PR-22: match `WeaponSystem::handleFire`'s server-
                        // side eye computation exactly — `pos.value +
                        // {0, halfExtents.y * 0.75, 0}`.  Earlier this was
                        // a hardcoded `+75`, which produced a ~48-unit
                        // ray-origin desync against the server (the
                        // default CollisionShape.halfExtents.y is 36, not
                        // 100, so the eye sits at +27 not +75).  Fixing
                        // this brought the desync below 1 unit at RTT=0.
                        const glm::vec3 origin = myPos->value + glm::vec3{0.0f, myShape->halfExtents.y * 0.75f, 0.0f};
                        const float cp = std::cos(input_.pitch);
                        const glm::vec3 dir{
                            std::sin(input_.yaw) * cp, -std::sin(input_.pitch), std::cos(input_.yaw) * cp};

                        // Intended target = closest other entity (mirror
                        // of the aim-at-target search above).  Logged so
                        // the analyzer can compare "who the bot meant to
                        // hit" vs "who the server actually hit".
                        std::uint16_t intendedCid = 0xFFFFu;
                        glm::vec3 intendedPos{0.0f};
                        float intendedDist = 0.0f;
                        float closestDist = std::numeric_limits<float>::max();

                        // PR-22: bot's local AABB raycast against
                        // visible players.  Bots have no skeleton-
                        // capsule data (`HitboxInstance` is not
                        // replicated), so this is broad-phase only —
                        // any hit at the AABB level is what a real
                        // client would also see if it skipped the
                        // capsule narrow phase.  Compared against the
                        // server's authoritative capsule raycast in
                        // the netsync analyzer to surface client-vs-
                        // server-rewind hit-reg mismatches.
                        constexpr float botRayMaxDist = 5000.0f;
                        bool botRayHit = false;
                        std::uint16_t botHitCid = 0xFFFFu;
                        glm::vec3 botHitPos{0.0f};
                        float botHitDist = 0.0f;
                        float bestBotDist = botRayMaxDist;

                        auto rview = registry_.view<Position, CollisionShape, ClientId>();
                        for (const auto e : rview) {
                            if (e == *localEnt)
                                continue;
                            const auto& tpos = rview.get<Position>(e);

                            // Track closest entity for "intended target".
                            const float dCenter = glm::length(tpos.value - myPos->value);
                            if (dCenter < closestDist) {
                                closestDist = dCenter;
                                intendedCid = static_cast<std::uint16_t>(rview.get<ClientId>(e).value);
                                intendedPos = tpos.value;
                                intendedDist = dCenter;
                            }

                            // Slab-method ray vs entity AABB.
                            const auto& shape = rview.get<CollisionShape>(e);
                            const glm::vec3 boxMin = tpos.value - shape.halfExtents;
                            const glm::vec3 boxMax = tpos.value + shape.halfExtents;
                            float tMin = 0.0f;
                            float tMax = bestBotDist;
                            bool slabOk = true;
                            for (int axis = 0; axis < 3; ++axis) {
                                if (std::abs(dir[axis]) < 1e-6f) {
                                    if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis]) {
                                        slabOk = false;
                                        break;
                                    }
                                    continue;
                                }
                                const float invDir = 1.0f / dir[axis];
                                float t1 = (boxMin[axis] - origin[axis]) * invDir;
                                float t2 = (boxMax[axis] - origin[axis]) * invDir;
                                if (t1 > t2)
                                    std::swap(t1, t2);
                                if (t1 > tMin)
                                    tMin = t1;
                                tMax = std::min(tMax, t2);
                                if (tMin > tMax) {
                                    slabOk = false;
                                    break;
                                }
                            }
                            if (!slabOk || tMin < 0.0f || tMin >= bestBotDist)
                                continue;
                            bestBotDist = tMin;
                            botRayHit = true;
                            botHitCid = static_cast<std::uint16_t>(rview.get<ClientId>(e).value);
                            botHitDist = tMin;
                            botHitPos = origin + dir * tMin;
                        }

                        writeShotIntent(static_cast<std::uint16_t>(myCid->value),
                                        input_.tick,
                                        origin,
                                        dir,
                                        intendedCid,
                                        intendedPos,
                                        intendedDist,
                                        botRayHit,
                                        botHitCid,
                                        botHitPos,
                                        botHitDist);
                    }
                }
            }
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

        // PR-18: log observation only when a fresh snapshot was applied
        // since the last poll.  `consumeSnapshotApplied()` is self-
        // resetting, so this fires at the snapshot rate (≈32-128 Hz
        // depending on server config) — much less noisy than
        // logging every tick which would just duplicate rows when
        // the snapshot stream lulls.
        if (client_.consumeSnapshotApplied())
            writeObservationLog();

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
