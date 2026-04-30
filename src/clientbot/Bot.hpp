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

    /// @brief Spawn the worker thread. Returns immediately.
    /// @param stopFlag Shared shutdown signal. Loop exits when set true.
    void start(const std::atomic<bool>& stopFlag);

    /// @brief Block until the worker thread exits. Caller is responsible
    ///        for setting the stopFlag observed by start().
    void join();

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
};
