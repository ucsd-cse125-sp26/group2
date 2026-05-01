/// @file Bot.hpp
/// @brief Headless network-only client bot for load-testing the server.
///
/// A Bot owns one Client connection and one Registry, runs a fixed-rate
/// tick loop on its own thread, and:
///   * sends an InputSnapshot every tick (with the same redundant-multi-input
///     wire format as the real client),
///   * drains incoming server snapshots via Client::poll so the connection
///     keeps consuming bandwidth at the same rate as a real client.
///
/// No rendering, no audio, no physics, no animation. The Registry exists
/// only so the Client's snapshot loader has somewhere to apply state to —
/// it is never read after that.

#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/Client.hpp"

#include <SDL3/SDL_stdinc.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

// (Bot members keep the trailing-underscore convention used elsewhere
// in this file; the .clang-tidy member rule trips on it but the existing
// code base treats trailing-underscore as the established style.)

class Bot
{
public:
    /// @brief Default tick rate; matches the server's physics tick rate.
    static constexpr int k_tickHz = 128;

    Bot() = default;
    Bot(const Bot&) = delete;
    Bot& operator=(const Bot&) = delete;
    Bot(Bot&&) = delete;
    Bot& operator=(Bot&&) = delete;
    ~Bot();

    /// @brief Connect to the server. Must succeed before run() is called.
    /// @param host Hostname or IP.
    /// @param port TCP port.
    /// @param botId Numeric identifier used only for log prefix.
    /// @return False on connection failure.
    bool init(const std::string& host, Uint16 port, int botId);

    /// @brief Apply a simulated round-trip latency on the bot's UDP path.
    /// Wraps `Client::setSimulatedLatencyMs` so the latency simulator
    /// is exercised under bot-only load tests (the real client UI's
    /// slider is the other entry point).
    /// @param totalMs Total RTT (0–200) split half-and-half across
    ///                outbound and inbound packet queues.
    void setSimulatedLatencyMs(int totalMs);

    /// @brief Apply a simulated UDP packet-loss percentage to the bot's
    /// UDP path. Wraps `Client::setSimulatedLossPercent`.
    /// @param percent Per-datagram drop probability (0–100).
    void setSimulatedLossPercent(int percent);

    /// @brief Spawn the worker thread. Returns immediately.
    /// @param stopFlag Shared shutdown signal. Loop exits when set true.
    void start(const std::atomic<bool>& stopFlag);

    /// @brief Block until the worker thread exits. Caller is responsible
    ///        for setting the stopFlag observed by start().
    void join();

    /// @brief PR-1 (server-perf): current smoothed RTT in ms.
    ///
    /// Returns the bot's `Client::getNetStats().avgRttMs` snapshot so
    /// the multi-bot fleet aggregator can compute fleet-wide p50/p99
    /// without each bot needing to log on its own cadence.
    /// Thread-safe: reads an atomic float behind the scenes; the
    /// per-bot worker thread is the sole writer.
    [[nodiscard]] float getCurrentRttMs() const;

    /// @brief PR-1: true once the bot's worker has logged at least
    /// one finished tick. Used by the aggregator to avoid showing
    /// "0 ms RTT" for bots still in the connect window.
    [[nodiscard]] bool isReady() const { return ready_.load(std::memory_order_relaxed); }

private:
    /// @brief Worker-thread main loop: send input + poll, sleep to next tick.
    void runLoop(const std::atomic<bool>& stopFlag);

    Client client_;            ///< Underlying TCP client (TCP today; UDP after Phase 3).
    Registry registry_;        ///< Snapshot apply target — never read.
    std::thread thread_;       ///< Worker thread; joined in dtor or join().
    InputSnapshot input_{};    ///< Reused per-tick input scratch.
    uint32_t predictTick_ = 0; ///< Monotonic tick counter, stamped onto each input.
    int botId_ = 0;            ///< Log prefix.
    bool initialized_ = false; ///< True once init() succeeded; gates run().

    /// PR-1 (server-perf): set true after the first successful poll inside
    /// runLoop. Lets the fleet aggregator skip bots that are mid-connect
    /// or whose RTT hasn't been measured yet.
    std::atomic<bool> ready_{false};
};
