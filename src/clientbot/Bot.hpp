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
// PR-23: prediction parity with the real client.  Header-only, so just
// pulling in InputRingBuffer is enough at the .hpp level.
#include "systems/InputRingBuffer.hpp"

#include <SDL3/SDL_stdinc.h>

#include <atomic>
#include <cstdint>
#include <glm/vec3.hpp>
#include <optional>
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
    std::optional<registry_serialization::Loader> snapshotLoader_;
    std::optional<entt::entity> mappedLocalPlayerEntity_;
    int botId_ = 0;            ///< Log prefix.
    bool initialized_ = false; ///< True once init() succeeded; gates run().

    /// PR-1 (server-perf): set true after the first successful poll inside
    /// runLoop. Lets the fleet aggregator skip bots that are mid-connect
    /// or whose RTT hasn't been measured yet.
    std::atomic<bool> ready_{false};

    // ── PR-23: prediction + reconciliation (parity with real client) ────
    //
    // Client-side prediction lets the bot simulate its OWN movement
    // locally each physics tick.  Pre-PR-23 the bot's `Position` only
    // moved when a server snapshot applied, which meant the bot's view
    // of its own position lagged the server's by `RTT/2 + interpDelay`.
    // For lag-comp / hit-reg testing under simulated RTT that drift was
    // a measurement artifact (not a real-client behaviour), polluting
    // the PR-22 ray-origin-desync metric with self-position lag the
    // real client doesn't have.
    //
    // The bot now mirrors `client/game/Game.cpp::iterate`:
    //   * each physics tick, push (predictTick_, input_) into ring,
    //     run `systems::runPrediction` on the bot's local entity
    //   * after `client_.poll`, if a snapshot just applied, call
    //     `systems::runReconciliation` from `serverAckedClientTick`
    //     forward to `predictTick_`.
    // Same code paths the real client runs — divergence between bot
    // and real client is now data-only (different inputs), not
    // architectural.
    InputRingBuffer inputRing_;

    /// @brief Set true once the bot maps the server-assigned local player.
    /// Gates the prediction loop so we don't try to runPrediction
    /// before the bot has its `LocalPlayer` / `InputSnapshot` /
    /// `PlayerSimState` components in the registry.
    bool localPlayerReady_ = false;

    /// @brief PR-23: set up the snapshot callback that emplaces
    /// `LocalPlayer + InputSnapshot + PreviousPosition + PlayerSimState`
    /// once the bot can map its server-assigned local entity.
    void setupLocalPlayerCallback();
    bool applyIncomingSnapshot(
        std::uint32_t snapshotTick, const std::uint8_t* bytes, Uint32 size, Uint64 captureNs, std::uint32_t& ackedTick);
    [[nodiscard]] std::optional<entt::entity> getLocalPlayerEntity() const;

    // ── PR-18: per-bot snapshot observation log ──────────────────────────
    //
    // Opened from `GROUP2_BOT_OBS_CSV_PREFIX` at init() if set; one CSV
    // file per bot, named `<prefix><botId>.csv`.  Each row records one
    // remote-entity sighting from the bot's perspective:
    //
    //     wallTimeNs,observerBotId,observedClientId,posX,posY,posZ
    //
    // Written ONCE per snapshot apply (not once per tick) — gated by
    // `Client::consumeSnapshotApplied()` so the row count matches the
    // snapshot stream, not the tick stream.
    //
    // The companion server-side `GROUP2_SERVER_TRUTH_CSV` log records the
    // SAME columns from the server's perspective (with `serverTick` for
    // alignment).  An offline Python tool (`scripts/netsync-analyze.py`)
    // joins the two by wall-clock + clientId, interpolates between
    // adjacent server samples, and reports euclidean desync per bot per
    // entity per (sim RTT, sim loss) bucket.
    //
    // This framework would have caught the FragmentReassembler stuck-
    // state bug (PR-17) instantly: bot's observation log freezes while
    // server truth keeps moving → enormous desync values.  Going forward
    // it lets us A/B-test future netcode changes (quantization, AoI
    // culling, snapshot-rate changes) with hard numbers, not feel.
    std::FILE* obsCsv_ = nullptr;

    // ── PR-21: bot-side shot-intent log ──────────────────────────────────
    //
    // Opened from `GROUP2_BOT_SHOTS_CSV_PREFIX` at init() if set.  One
    // CSV per bot, named `<prefix><botId>.csv`.  Each row records ONE
    // intentional fire (rising edge of `input.shooting`) with the
    // information needed to join against the server-side
    // `server_shots.csv` produced by PR-18b's `perf::shotlog`:
    //
    //   wallTimeNs,shooterClientId,shotInputTick,
    //   originX,originY,originZ,
    //   dirX,dirY,dirZ,
    //   intendedTargetClientId,
    //   intendedTargetX,intendedTargetY,intendedTargetZ,
    //   intendedTargetDist
    //
    // The matching key is `(shooterClientId, shotInputTick)` — the
    // server stamps the same pair on every shot in `server_shots.csv`.
    // The Python analyzer joins them and reports per-RTT hit rate,
    // intended-vs-actual target distribution, region match, etc.
    std::FILE* shotsCsv_ = nullptr;
    bool prevShootingForLog_ = false; ///< Rising-edge detector for fire log.

    /// @brief Open the shot-intent log if `GROUP2_BOT_SHOTS_CSV_PREFIX`
    /// is set.  No-op when env var is missing.
    void openShotsLog();

    /// @brief Append a shot-intent row.  Called from runLoop on the
    /// rising edge of `input_.shooting`.  No-op when the log isn't
    /// open.  `intendedTargetEntity == entt::null` means the bot
    /// fired without aiming at any visible target (random fire).
    ///
    /// PR-22: also records the bot's local AABB raycast result —
    /// "who the bot would think it hit" with the limited information
    /// it has on its side.  Bots have no replicated `HitboxInstance`
    /// (capsule-level skeleton), so this is necessarily AABB-only;
    /// the server still does full capsule lag-comp.  Mismatches
    /// between bot-side and server-side hits are the headline
    /// hit-reg signal the netsync analyzer reports.
    void writeShotIntent(std::uint16_t shooterClientId,
                         std::uint32_t shotInputTick,
                         const glm::vec3& origin,
                         const glm::vec3& direction,
                         std::uint16_t intendedTargetClientId,
                         const glm::vec3& intendedTargetPos,
                         float intendedTargetDist,
                         bool botRayHit,
                         std::uint16_t botHitClientId,
                         const glm::vec3& botHitPos,
                         float botHitDist);

    /// @brief Flush + close the shot log if open.  Safe from dtor.
    void closeShotsLog() noexcept;

    /// @brief Open the CSV if `GROUP2_BOT_OBS_CSV_PREFIX` is set.  No-op
    /// if env unset or file open fails (graceful: load tests stay fast).
    void openObservationLog();

    /// @brief Walk the registry for `view<Position, ClientId>`, write one
    /// row per replicated player entity to the bot's CSV.  Caller must
    /// have already verified `Client::consumeSnapshotApplied()` so we
    /// emit at snapshot rate, not tick rate.
    void writeObservationLog();

    /// @brief Flush + close the CSV if open.  Safe to call from dtor.
    void closeObservationLog() noexcept;
};
