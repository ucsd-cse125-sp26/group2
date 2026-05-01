/// @file Bot.cpp
/// @brief Implementation of the headless client bot.

#include "Bot.hpp"

#include <SDL3/SDL.h>

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

void Bot::start(const std::atomic<bool>& stopFlag)
{
    if (!initialized_) {
        SDL_Log("[bot %d] start() called without successful init()", botId_);
        return;
    }
    thread_ = std::thread([this, &stopFlag] { runLoop(stopFlag); });
}

void Bot::join()
{
    if (thread_.joinable())
        thread_.join();
}

void Bot::runLoop(const std::atomic<bool>& stopFlag)
{
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 tickDurationCounters = perfFreq / static_cast<Uint64>(k_tickHz);
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

        // ── Pacing: hybrid sleep + spin to hit the next tick boundary. ──
        //
        // Same approach as ServerGame::run — coarse SDL_Delay for the bulk,
        // spin-wait the sub-millisecond remainder for steady cadence. With
        // 100+ bots each spinning, this WILL contend; that's fine, the load
        // test is supposed to stress the network thread, not CPU efficiency.
        const Uint64 now = SDL_GetPerformanceCounter();
        if (now < nextTick) {
            const Sint64 sleepMs = static_cast<Sint64>((nextTick - now) * 1000 / perfFreq) - 1;
            if (sleepMs > 0)
                SDL_Delay(static_cast<Uint32>(sleepMs));
            while (SDL_GetPerformanceCounter() < nextTick && !stopFlag.load(std::memory_order_relaxed)) {
                // spin
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
        client_.updateStats(1.0f / static_cast<float>(k_tickHz));

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
