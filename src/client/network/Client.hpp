/// @file Client.hpp
/// @brief TCP client for connecting to the game server.

#pragma once

#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/ChatProtocol.hpp"
#include "network/DiscoverySettings.hpp"
#include "network/MatchConfig.hpp"
#include "network/MatchStatus.hpp"
#include "network/MessageStream.hpp"
#include "network/NetKillEvent.hpp"
#include "network/NetworkConfig.hpp"
#include "network/OutboundQueue.hpp"
#include "network/RegistrySerialization.hpp"
#include "network/ShotDebugReport.hpp" // PR-20: shared wire-format + runtime capture struct.
#include "network/ShotEvent.hpp"
#include "network/VoiceProtocol.hpp"
#include "network/lobby/LobbyStatus.hpp"
#include "network/transport/FragmentReassembler.hpp"
#include "network/transport/UdpEndpoint.hpp"
#include "network/transport/UdpSessionTransport.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <entt/entt.hpp>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

/// @brief Live network statistics updated each frame.
struct NetworkStats
{
    float rttMs = 0.0f;                 ///< Latest round-trip time (ms).
    float avgRttMs = 0.0f;              ///< Exponential moving average RTT (ms).
    uint64_t bytesRecvTotal = 0;        ///< Total bytes received since connection.
    uint64_t bytesSentTotal = 0;        ///< Total bytes sent since connection.
    float recvBytesPerSec = 0.0f;       ///< Receive bandwidth (bytes/sec, smoothed).
    float sendBytesPerSec = 0.0f;       ///< Send bandwidth (bytes/sec, smoothed).
    uint32_t registryUpdateSize = 0;    ///< Last registry update payload size (bytes).
    float registryUpdatesPerSec = 0.0f; ///< Registry updates received per second.
};

enum class ConnectError
{
    None,
    ResolveFailed,
    ResolveTimedOut,
    CreateClientFailed,
    ConnectTimedOut,
    ConnectFailed,
    LobbyFull,
};

/// @brief TCP stream client — sends input to the server and receives state updates.
class Client
{
public:
    /// @brief Called by Client to apply a raw snapshot; the registry-owning caller performs the actual load.
    /// Returns true on success; ackedTick is populated with the server-acked client predict tick.
    using SnapshotApplyCallback = std::function<bool(std::uint32_t snapshotTick,
                                                     const std::uint8_t* bytes,
                                                     Uint32 size,
                                                     Uint64 captureNs,
                                                     std::uint32_t& ackedTick)>;
    /// @brief Called for each replicated particle event before entity mapping; caller is responsible for mapping.
    using RawParticleEventCallback = std::function<void(const NetParticleEvent& evt)>;
    using MatchStateUpdateFn = std::function<void(const MatchStatePacket&)>;
    using KillEventCallback = std::function<void(const NetKillEvent&)>;
    using TextChatCallback = std::function<void(const net::chat::ServerTextChat&)>;
    using VoiceFrameCallback = std::function<void(const net::voice::ServerVoiceFrame&)>;
    /// @brief PR-20: callback for SHOT_DEBUG_REPORT.  Fired on the
    /// game thread inside `dispatchMessage` after the bytes have been
    /// parsed back into a `ShotDebugCapture`.  The DebugUI registers
    /// this and pairs the report with its own client-side fire-time
    /// snapshot by `shotInputTick`.
    using ShotDebugCallback = std::function<void(const net::shotdebug::ShotDebugCapture&)>;

    /// @brief Fired for each incremental lobby roster update broadcast from the server.
    using LobbyUpdateCallback = std::function<void(const LobbyUpdateEvent& update)>;
    /// @brief Fired once on join with the full lobby snapshot and this client's assigned ID.
    using LobbyStateCallback = std::function<void(const std::vector<LobbyPlayer>& players, ClientId localId)>;
    using MatchConfigCallback = std::function<void(const MatchConfig& config)>;

    /// @brief Create the TCP socket and connect to the server.
    /// @param addr      Hostname or IP address of the server.
    /// @param port      TCP port the server is listening on. The UDP
    ///                  sidecar (Phase 3d) connects to the same port.
    /// @param transport Phase 3d: which UDP features to enable.
    /// @param timeoutMs Maximum time to wait for DNS resolution and TCP
    ///                  connection, in milliseconds. Negative waits forever.
    /// @return None on success, otherwise the specific connection failure.
    ConnectError init(const char* addr,
                      Uint16 port,
                      const TransportConfig& transport = {},
                      int timeoutMs = -1,
                      const std::optional<net::UdpSessionTransport::RelayConfig>& relay = std::nullopt);

    /// @brief Close the socket and release the resolved address.
    void shutdown();

    /// @brief True while a server connection is currently owned by this client.
    bool isConnected();

    /// @brief Send a raw message to the server.
    /// @param data  Pointer to the payload bytes.
    /// @param size  Payload length in bytes.
    /// @return False if the send fails.
    bool send(const void* data, uint32_t size);

    /// @brief Push the latest input into the redundant ring and send to the server.
    ///
    /// Each call appends @p snap to a small ring buffer (capacity
    /// @ref k_inputRedundancy) and emits one INPUT packet containing the
    /// last N stored snapshots in tick order, oldest-first. The server
    /// dedups by `InputSnapshot.tick` against `lastAppliedInputTick`, so
    /// resending the last few inputs costs ~5x bandwidth on this packet
    /// type while making the input stream resilient to single-packet loss
    /// or reorder. Caller is responsible for stamping `snap.tick` with the
    /// current `clientPredictTick` before calling.
    bool sendInputSnapshot(const InputSnapshot& snap);

    /// @brief Clear redundant input history at the start of a new local match instance.
    void resetInputHistory();

    /// @brief PR-27 (netsync): send a SHOT_INTENT packet describing the
    /// client's view of the target's animation state at fire time.
    /// Server pairs this with the corresponding INPUT (by
    /// `(shooterClientId, shotInputTick)`) and computes the anim-state
    /// delta against its own historical state at the rewound tick.
    /// Sent once per rising-edge of `input.shooting`.  `targetClientId`
    /// = `0xFFFF` when the client wasn't aiming at any specific target.
    bool sendShotIntent(std::uint32_t shotInputTick, std::uint16_t targetClientId, const AnimSnapshot& targetAnim);

    /// @brief Send an all-chat message to the authoritative server.
    bool sendChatMessage(std::string_view message);

    /// @brief Ask the server to start/stop authoritative physics CSV recording.
    bool sendPhysicsDiagRecording(bool enabled);

    /// @brief Send one Opus-encoded voice frame. Voice rides unreliable sequenced UDP.
    bool sendVoiceFrame(std::uint16_t sequence, std::uint8_t frameMs, std::span<const std::uint8_t> opus);

    /// @brief Send a PLAYER_READY or PLAYER_UNREADY packet to the server.
    bool sendPlayerReady(bool ready);

    /// @brief Send a START_MATCH packet to the server (host-only).
    bool sendStartMatch();

    /// @brief Send an updated match configuration to the server (host-only).
    bool sendMatchConfig(const MatchConfig& config);

    /// @brief Send updated discovery advertisement settings to the server.
    ///
    /// The server applies this only when the sender is the current lobby host.
    /// The packet controls whether the server publishes to the global directory
    /// and whether it responds to LAN discovery requests.
    bool sendDiscoverySettings(const DiscoverySettings& settings);

    /// @brief Request server shutdown. The server accepts this only from the current host.
    bool sendServerShutdown();

    /// @brief Send a PING packet to the server for RTT measurement.
    void sendPing();

    /// @brief Update bandwidth stats. Call once per frame with the frame delta time.
    void updateStats(float dt);

    void onSnapshotApply(SnapshotApplyCallback fn)
    {
        snapshotApplyFn_ = std::move(fn);
    } ///< Register the snapshot-apply callback; must be set before the first poll().
    void onRawParticleEvent(RawParticleEventCallback fn)
    {
        rawParticleEventFn_ = std::move(fn);
    } ///< Register the raw particle-event callback.
    /// @brief Register the match-state update callback, fired on every MATCH_STATE packet.
    void onMatchStateUpdate(MatchStateUpdateFn fn) { matchStateUpdateFn_ = std::move(fn); }
    /// @brief Register the kill-event callback, fired for each replicated kill from the server.
    void onKillEvent(KillEventCallback fn) { killEventFn_ = std::move(fn); }
    void onTextChat(TextChatCallback fn) { textChatFn_ = std::move(fn); }
    void onVoiceFrame(VoiceFrameCallback fn) { voiceFrameFn_ = std::move(fn); }
    /// @brief Register the shot-debug callback (PR-20); fired for each SHOT_DEBUG_REPORT.
    void onShotDebugReport(ShotDebugCallback fn) { shotDebugFn_ = std::move(fn); }
    void onLobbyUpdate(LobbyUpdateCallback fn)
    {
        lobbyUpdateFn_ = std::move(fn);
    } ///< Register the incremental lobby-update callback.
    void onLobbyState(LobbyStateCallback fn)
    {
        lobbyStateFn_ = std::move(fn);
    } ///< Register the full lobby-snapshot callback, fired once on join.
    void onMatchConfig(MatchConfigCallback fn)
    {
        matchConfigFn_ = std::move(fn);
    } ///< Register the match-config update callback.

    /// @brief Receive and process one pending message.
    /// @return True if a message was received, false if the queue is empty.
    bool poll();

    /// @brief Access current network statistics.
    const NetworkStats& getNetStats() const { return stats; }

    /// @brief Return the latest match state packet received from the server, if any.
    std::optional<MatchStatePacket> getLatestMatchState() const { return latestMatchState_; }

    /// @brief Return the latest match configuration received from the server, if any.
    std::optional<MatchConfig> getLatestMatchConfig() const { return latestMatchConfig_; }

    /// @brief Return the latest lobby roster received from the server, if any.
    std::optional<std::pair<std::vector<LobbyPlayer>, ClientId>> getLatestLobbyState() const;

    /// @brief Latest server-acked client predict tick.
    ///
    /// Phase 5b: when the server applies an INPUT packet stamped with
    /// client-tick T, then later sends a snapshot, the snapshot's local-
    /// player position represents state-after-applying-input-T. The
    /// client uses this value to know where to start replaying stored
    /// inputs from for reconciliation. 0 if no snapshot has been applied
    /// yet, or if the local player wasn't in the most recent snapshot.
    [[nodiscard]] uint32_t getServerAckedClientTick() const noexcept { return serverAckedClientTick_; }

    /// @brief Whether a snapshot was applied since the last call to consumeSnapshotApplied().
    ///
    /// Phase 5b: the game thread reads this each iterate() to know when
    /// to trigger reconciliation. Self-resets so a single snapshot only
    /// triggers a single reconciliation pass.
    [[nodiscard]] bool consumeSnapshotApplied() noexcept
    {
        const bool was = snapshotAppliedFlag_;
        snapshotAppliedFlag_ = false;
        return was;
    }

    /// @brief Render-time interpolation alpha based on snapshot timing.
    ///
    /// Phase 5a: with the snapshot rate decoupled from the physics tick rate
    /// (Phase 4a default = 32 Hz vs 128 Hz physics), the renderer can no
    /// longer use `accumulator / k_physicsDt` as the lerp alpha — that
    /// span is ~7.8 ms while two consecutive snapshots are ~31 ms apart.
    /// The result was the entity stepping in 7.8 ms bursts every 31 ms.
    ///
    /// This helper returns alpha as
    ///   `(now - lastSnapshotApplyNs) / (lastSnapshotApplyNs - prevSnapshotApplyNs)`
    /// clamped to [0, 1]. Self-correcting if the server changes its
    /// snapshot rate; freezes at 1.0 (entity at "current" pos, no extrapolation)
    /// when a snapshot is overdue. Returns 1.0 before two snapshots have
    /// arrived (no interpolation reference yet).
    ///
    /// @note PR-11 supersedes this for non-local entities.  When the
    /// `InterpolationBuffer` path is in effect, the renderer uses
    /// `getInterpolationRenderTimeNs()` to play back at `now − delay`
    /// instead of lerping forward over the most recent interval.  The
    /// alpha here remains the local-player / fallback path.
    [[nodiscard]] float getSnapshotAlpha() const;

    /// @brief Server-assigned local-player entity before continuous_loader mapping.
    /// The registry-owning caller maps this through its snapshot loader.
    [[nodiscard]] std::optional<entt::entity> getServerLocalPlayerEntity() const { return localPlayerEntity; }

    /// @brief Render time the renderer should display non-local entities at.
    ///
    /// PR-11 (server-perf): Valorant / Fortnite / Source-engine `cl_interp`
    /// style render-delay interpolation.  Returns
    ///   `SDL_GetTicksNS() − delayTicks × snapshotIntervalNs()`
    /// where `delayTicks` is read from `GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS`
    /// (default 2) and `snapshotIntervalNs` is the EMA of the last two
    /// snapshot apply times.
    ///
    /// Returns 0 until two snapshots have been applied — callers treat 0
    /// as "no buffered playback yet, fall back to the Phase-5a alpha
    /// path" (see `entity_interpolation::sample`).
    ///
    /// Why N=2?  At 32 Hz snapshot rate, 2 ticks ≈ 62.5 ms — enough that
    /// the renderer always has at least one buffered "future" sample to
    /// interpolate toward, so a single dropped snapshot is invisible.
    /// Trade-off: visual feedback for remote players is delayed by 62.5 ms
    /// from server truth, but lag-comp on the server already accounts
    /// for the client's display-time-to-fire-time gap (Phase 6 lag comp).
    [[nodiscard]] Uint64 getInterpolationRenderTimeNs() const;

    /// @brief Approximate snapshot interval in nanoseconds.
    ///
    /// EMA over the last two snapshot apply times.  Falls back to the
    /// default 32 Hz period (~31.25 ms) before two snapshots have arrived.
    [[nodiscard]] Uint64 getSnapshotIntervalNs() const;

    /// @brief PR-19: overwrite `Position.value` (and `InputSnapshot.yaw`)
    /// for every non-local entity with an `InterpolationBuffer` to its
    /// interpolated render-time value.  Runs once per frame, BEFORE any
    /// renderer / particle / sfx / tracer code reads `pos.value` —
    /// every visual consumer thereafter sees a single, consistent
    /// interpolated source of truth.
    ///
    /// Pre-PR-19 the renderer interpolated at 3 specific call sites
    /// while tracers, ribbon trails, smoke emitters, and beam endpoints
    /// kept reading raw `pos.value`.  At 128 Hz × 2-snapshot delay
    /// (~16 ms) that's ~6-unit visible separation between the body
    /// and effects originating from "where the body really is right now".
    ///
    /// Why mutate `Position.value` in place rather than ship a separate
    /// `RenderPosition` component?  Two reasons: (1) every consumer
    /// already reads `pos.value`, no per-call-site touch-up needed; (2)
    /// the next snapshot apply unconditionally overwrites `pos.value`
    /// with the server-authoritative value (entt's continuous_loader),
    /// so the mutation has no lasting effect on registry state — it's
    /// effectively a per-frame derived view.  Concrete cycle:
    ///
    ///   1. Snapshot apply → pos.value = server's value at tick T.
    ///   2. recordInterpolationSamples reads server value, appends.
    ///   3. (this method) → pos.value = interp_sample(buffer, renderTime).
    ///   4. Renderer + particles + tracers + sfx read pos.value.
    ///   5. Next snapshot apply re-overwrites pos.value with new server
    ///      value (step 1 again).
    ///
    /// No-op when render-delay interp is disabled (`interpDelaySnapshots_
    /// == 0`) or no buffered playback yet (renderTimeNs == 0).
    /// Excludes local player (which has no `InterpolationBuffer` because
    /// `recordInterpolationSamples` filters local out, and which is
    /// driven by client-side prediction anyway).
    void applyInterpolatedTransforms(Registry& registry);

    /// @brief Record interpolation samples after the caller has applied a snapshot.
    void recordInterpolationSamples(Registry& registry, Uint64 captureNs);

    /// @brief Number of recent inputs included in each INPUT packet for redundancy.
    ///
    /// At 128 Hz client tick rate, 5 inputs covers ~40 ms of redundancy —
    /// enough to recover from single-packet loss without retransmission, at
    /// the cost of ~5x INPUT-packet payload (still tiny: ~200 bytes/packet).
    static constexpr size_t k_inputRedundancy = 5;

    /// @brief Phase 6 testing: simulate added round-trip latency.
    ///
    /// Setting this to N causes outbound UDP datagrams to be held for
    /// N/2 ms before the kernel sees them, and incoming UDP messages
    /// to be held for N/2 ms before being delivered to the game-thread
    /// dispatch queue. The two halves combined produce an extra N ms
    /// of round-trip on top of whatever the real network has.
    ///
    /// Range: 0–200 ms (slider-bounded; values outside the range are
    /// clamped on entry). 0 disables the simulator entirely — packets
    /// take the same fast path they did before this feature existed,
    /// no per-packet allocation, no extra mutex contention.
    ///
    /// Why split into outbound + inbound halves? It models a symmetric
    /// real network: client→server and server→client each take half
    /// the RTT. With outbound-only delay, the server would see stale
    /// inputs but reply at full speed, leaving lag-comp's RTT/2 rewind
    /// formula systematically under-correcting by the inbound half.
    /// Symmetric delay matches the formula and gives the same hit-feel
    /// as a real WAN player at the slider's RTT.
    void setSimulatedLatencyMs(int totalMs) noexcept;

    /// @brief Get the currently-effective simulated total RTT.
    [[nodiscard]] int getSimulatedLatencyMs() const noexcept
    {
        return simulatedLatencyMs_.load(std::memory_order_relaxed);
    }

    /// @brief Phase 6 testing: simulate UDP packet loss.
    ///
    /// Setting this to N makes each outbound and each inbound UDP
    /// datagram an independent N% Bernoulli drop. With redundancy
    /// disabled (PING/PONG) you'll see N% loss directly. With
    /// redundancy on (5-input INPUT packets, 3x reliable events,
    /// fragmented snapshots) effective loss is much lower:
    ///   - Inputs: a tick is lost only if 5 consecutive packets are
    ///     dropped — at 50% loss that's ~3 % per-tick loss.
    ///   - Reliable events: lost only if all 3 redundant copies
    ///     are dropped — at 50% loss that's ~12.5 % per-event loss.
    ///   - Fragmented snapshots: any single fragment loss kills the
    ///     whole snapshot — at 50% loss with 5 fragments that's
    ///     ~97 % per-snapshot loss. Use small values (5–15 %) for
    ///     testing snapshot resilience.
    ///
    /// Range: 0–100. 0 disables. Higher values are accepted but pin
    /// the connection (the slider in the debug UI caps at 50 %).
    void setSimulatedLossPercent(int percent) noexcept;

    /// @brief Get the currently-effective simulated packet loss %.
    [[nodiscard]] int getSimulatedLossPercent() const noexcept
    {
        return simulatedLossPercent_.load(std::memory_order_relaxed);
    }

private:
    MessageStream msgStream{nullptr};              ///< Framed message stream for server communication.
    NET_Address* serverAddr = nullptr;             ///< Resolved server address.
    SnapshotApplyCallback snapshotApplyFn_;        ///< Applies snapshot bytes in the registry-owning caller.
    RawParticleEventCallback rawParticleEventFn_;  ///< Called for unmapped replicated particle events.
    MatchStateUpdateFn matchStateUpdateFn_;        ///< Called whenever a MATCH_STATE packet is received.
    KillEventCallback killEventFn_;                ///< Called for each replicated kill event from server.
    TextChatCallback textChatFn_;                  ///< Called for server-broadcast all-chat messages.
    VoiceFrameCallback voiceFrameFn_;              ///< Called for proximity-routed Opus voice frames.
    ShotDebugCallback shotDebugFn_;                ///< PR-20: called for each SHOT_DEBUG_REPORT from server.
    LobbyUpdateCallback lobbyUpdateFn_;            ///< Called for each lobby update received from server.
    LobbyStateCallback lobbyStateFn_;              ///< Called once on join with the full lobby snapshot.
    MatchConfigCallback matchConfigFn_;            ///< Called whenever a MATCH_CONFIG packet is received.
    std::optional<entt::entity> localPlayerEntity; ///< The local player's entity, once assigned by the server.
    std::optional<MatchStatePacket>
        latestMatchState_;                         ///< Most-recent MATCH_STATE packet; populated by dispatchMessage.
    std::optional<MatchConfig> latestMatchConfig_; ///< Most-recent MATCH_CONFIG packet; populated by dispatchMessage.
    std::optional<std::vector<LobbyPlayer>> latestLobbyPlayers_; ///< Most-recent lobby roster received from the server.
    std::optional<ClientId> latestLobbyLocalId_; ///< This client's ID as reported in the LOBBY_STATE packet.

    // ── PR-10 + PR-14 (server-perf): snapshot delta encoding state ────
    //
    // `keyframePayload_` holds the most-recent FULL snapshot's raw
    // entt-serialized bytes (no PacketType prefix, no tick), and
    // `keyframeTick_` is the tick that snapshot was sent at.
    //
    // PR-14 (loss resilience): both fields update *only* on FULL
    // arrival.  DELTA packets reconstruct the current frame's bytes
    // by applying their patch on top of the keyframe and feed the
    // reconstructed bytes into the Loader, but do NOT replace the
    // saved keyframe.  Pre-PR-14, every DELTA replaced the saved
    // baseline with the just-reconstructed bytes — which meant a
    // single dropped DELTA cascaded into all subsequent DELTAs in the
    // same keyframe window dropping silently (their `fromTick` no
    // longer matched the client's stored `lastSnapshotTick_`).  Now
    // every DELTA in a window is independently decodable against the
    // shared keyframe, so individual packet drops only cost that one
    // frame's state.
    //
    // If `fromTick` on a DELTA doesn't match `keyframeTick_` the
    // packet is dropped — happens when a FULL keyframe was lost or
    // hasn't arrived yet.  The next periodic full keyframe (every 8
    // snapshots ≈ 62 ms at 128 Hz) re-syncs us.
    std::vector<uint8_t> keyframePayload_;
    std::uint32_t keyframeTick_ = 0;

    NetworkStats stats; ///< Live network metrics.

    // Bandwidth tracking — accumulated between updateStats() calls.
    uint64_t bytesSentWindow = 0;
    uint64_t bytesRecvWindow = 0;
    uint64_t udpSessionLastBytesSent_ = 0;
    uint64_t udpSessionLastBytesRecv_ = 0;
    uint32_t registryUpdatesWindow = 0;
    float statsAccumulator = 0.0f;

    // Redundant input ring — see k_inputRedundancy and sendInputSnapshot().
    // Stores the last N stamped InputSnapshots in chronological order so each
    // outbound INPUT packet can include them all (server dedups by tick).
    std::array<InputSnapshot, k_inputRedundancy> inputRing_{};
    size_t inputRingHead_ = 0;  ///< Next write index, wraps mod k_inputRedundancy.
    size_t inputRingCount_ = 0; ///< Valid entries in ring; saturates at k_inputRedundancy.

    // ── Stage 3c: dedicated network thread ────────────────────────────────
    //
    // Symmetric to the server's stage 3b. The network thread continuously
    // (a) pumps the kernel receive buffer into msgStream's recvBuf and
    // (b) drains the outbound queue to the socket. The game thread keeps
    // calling sendInputSnapshot / sendPing / poll() — those now
    // touch the queue + recvBuf under stateMutex_ rather than doing
    // syscalls inline. The win is that a render-frame stutter on the game
    // thread no longer causes the kernel buffer to back up.
    OutboundQueue outbound_;
    std::mutex stateMutex_;
    std::thread networkThread_;
    std::atomic<bool> shouldStop_{false};

    /// @brief Latched-true once the network thread observes a socket error.
    /// poll() checks this and reports false to the game thread, so
    /// the existing "server died" disconnect path still works.
    std::atomic<bool> socketDead_{false};

    // ── Phase 5a: snapshot-interval interpolation timing ──────────────────
    //
    // Updated whenever dispatchMessage applies an UPDATE_REGISTRY. The
    // renderer reads getSnapshotAlpha() instead of the physics-tick alpha
    // so motion stays smooth at the much-coarser snapshot rate. Both fields
    // are 0 before any snapshot has been applied; getSnapshotAlpha returns
    // 1.0 in that case so first frame draws the snapped position.
    Uint64 lastSnapshotApplyNs_ = 0;
    Uint64 prevSnapshotApplyNs_ = 0;

    // ── PR-11: render-delay interpolation ────────────────────────────────
    //
    // `interpDelaySnapshots_` is read once at init() from the
    // GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS env var (default 2).  0 disables
    // the buffered render-delay path entirely; non-local entities
    // fall back to the Phase-5a (prev, cur, alpha) lerp.  Higher values
    // smooth more loss but make remote entities visibly behind server
    // truth.  Source engine ships 2 (62.5 ms at 32 Hz) — that's our
    // default and matches Valorant / Fortnite cadence.
    //
    // EMA of snapshot apply intervals.  `prevSnapshotApplyNs_` and
    // `lastSnapshotApplyNs_` already give a single most-recent interval;
    // the EMA smooths burst arrivals so the render-delay computation
    // doesn't wobble when packets arrive bunched.  Snapshot rate doesn't
    // change during a session, so a single-pole IIR with α=0.25 is
    // ample.  Initial value matches the design's 32 Hz snapshot rate.
    // PR-13: 128 Hz default snapshot rate (AAA-pro cadence).  EMA
    // self-corrects once two snapshots have arrived if the actual
    // rate differs (e.g. legacy server running at 32 Hz from a
    // pre-PR-13 config.toml).  The two-snapshot warmup window is
    // ~16 ms at 128 Hz — fast enough that the initial value barely
    // matters in practice.
    static constexpr Uint64 k_defaultSnapshotIntervalNs = 1'000'000'000ULL / 128ULL;

    // PR-19: re-enabled default = 2 snapshots after `Client::
    // applyInterpolatedTransforms` started overwriting Position +
    // InputSnapshot.yaw in place every frame.  Now ALL visual
    // consumers (renderer, tracers, ribbon trails, smoke emitters,
    // beam endpoints, sfx) read from a single source of truth —
    // `pos.value`, freshly written each frame to the interpolated
    // value — so there's no more 6-unit body-vs-tracer separation
    // that PR-16 was the emergency hotfix for.
    //
    // PR-16's history (kept for posterity): pre-PR-19, PR-11 wired
    // `entity_interpolation::sample()` into 3 specific Game.cpp
    // render sites only, missing TracerEffect / RibbonTrail /
    // SmokeEffect / BeamState / sfx.  PR-16 default-flipped this to
    // 0 to disable the misaligned interp until PR-19's unified
    // approach landed.  PR-17 (FragmentReassembler stuck-state)
    // turned out to be the bigger source of "models in wrong
    // locations" — once that was fixed and PR-19 unifies the read
    // path, default-on is safe again.
    //
    // To disable: set `GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS=0`.
    int interpDelaySnapshots_ = 2;
    Uint64 snapshotIntervalEmaNs_ = k_defaultSnapshotIntervalNs;

    // ── Phase 5b: prediction reconciliation hand-off ──────────────────────
    //
    // Updated by dispatchMessage on every UPDATE_REGISTRY apply. The game
    // thread reads getServerAckedClientTick() + consumeSnapshotApplied() to
    // know when and from which tick to replay client-stored inputs.
    uint32_t serverAckedClientTick_ = 0;
    bool snapshotAppliedFlag_ = false;

    // ── Phase 3d: UDP sidecar ─────────────────────────────────────────────
    //
    // Bound during init() to any free local port. Server's address is
    // resolved from the same hostname/port we connected over TCP. We
    // stamp every outbound UDP datagram with the connectionId the
    // server gave us in the ASSIGN_CLIENT_ID packet so it can match
    // datagrams to our TCP-established Connection. Until that arrives
    // we fall back to TCP for everything (connectionId == 0).
    TransportConfig transportConfig_;
    bool usingUdpSession_ = false;
    net::UdpSessionTransport session_;
    net::UdpEndpoint udpEndpoint_;
    net::UdpEndpointAddr serverUdpAddr_;
    std::uint64_t connectionId_ = 0;
    uint16_t udpInputSequence_ = 0; ///< Per-channel sequence for INPUT datagrams.
    std::uint16_t chatClientSeq_ = 0;

    /// @brief UDP-received payloads waiting for the game thread to
    /// dispatch. Filled by the network thread under stateMutex_; drained
    /// by Client::poll. Format of each entry is `[PacketType][rest]` —
    /// same as a complete framed message off the TCP path so the same
    /// dispatchMessage() handles both.
    std::vector<std::vector<uint8_t>> udpRecvQueue_;

    /// @brief Phase 3d-4: reassembly buffer for fragmented snapshot
    /// datagrams on the Unreliable channel. Tracks one in-progress
    /// reassembly per client (the most-recent sequence). Older
    /// fragments dropped via the FragmentReassembler's drop-stale
    /// rule.
    net::FragmentReassembler unreliableReassembler_;

    /// @brief Phase 3d-5: sliding-window bitset for ReliableOrdered
    /// channel dedup. Each event arrives k_reliableRedundancy times;
    /// only the first occurrence triggers dispatch. The window is
    /// 64 sequences wide, enough to cover RTT × redundancy at any
    /// reasonable network speed. Sequences older than that get
    /// dropped (very rare — would require 64 events to arrive
    /// during one RTT).
    std::uint32_t reliableHighestSeen_ = 0;
    uint64_t reliableSeenBitmask_ = 0;
    bool reliableHasAny_ = false; ///< False until the first reliable event arrives.

    /// @brief Sliding-window dedup helper. Returns true if the
    /// caller should dispatch this sequence (i.e. it's new); false
    /// if it's a duplicate or too old to track.
    bool acceptReliableSequence(std::uint32_t seq);

    // ── Phase 6 testing: latency simulator ────────────────────────────────
    //
    // Two FIFO queues — outbound packets that haven't reached the kernel
    // socket yet, and inbound payloads that haven't been delivered to the
    // game thread's dispatch queue yet. Both are drained at the top of
    // each `networkLoop` cycle: any entry whose target counter has passed
    // is sent (or enqueued for dispatch). Both share `stateMutex_` because
    // the existing send/receive paths already hold it; piggy-backing
    // avoids a second mutex.
    //
    // Sized to grow with traffic — typical worst case is 200 ms × 128 Hz
    // INPUT × 1 client + ~32 Hz inbound snapshot stream ≈ 30 entries.

    /// @brief Total simulated RTT in ms (slider value, 0–200).
    /// Atomic so the UI thread can write while the network thread reads.
    std::atomic<int> simulatedLatencyMs_{0};

    /// @brief Per-direction independent UDP-drop probability (slider
    /// value, 0–100). Each outbound and each inbound datagram rolls
    /// against this; rolls below the threshold are dropped silently.
    std::atomic<int> simulatedLossPercent_{0};

    /// @brief PRNG for the loss simulator. Always accessed under
    /// `stateMutex_` (every loss-roll site already holds it for
    /// other reasons). Seeded once in `init()` so behaviour varies
    /// run-to-run; not cryptographically secure, but the simulator
    /// is a debug aid, not a security boundary.
    std::mt19937 simLossRng_{};

    /// @brief Roll the loss RNG. Caller MUST already hold `stateMutex_`.
    /// @return True when the caller should treat the packet as dropped.
    bool shouldDropPacketLocked();

    /// @brief One outbound UDP datagram queued for delayed send.
    struct DelayedOutbound
    {
        Uint64 sendAtCounter;         ///< Performance counter at which to send.
        net::PacketHeader header;     ///< Caller-supplied header (passed through verbatim).
        std::vector<uint8_t> payload; ///< Datagram payload bytes.
        std::size_t totalBytes;       ///< For deferred bandwidth accounting.
    };
    std::deque<DelayedOutbound> simLatOutbound_;

    /// @brief One inbound payload queued for delayed dispatch.
    struct DelayedInbound
    {
        Uint64 deliverAtCounter;      ///< Performance counter at which to enqueue.
        std::vector<uint8_t> payload; ///< Already-assembled message ([PacketType][rest]).
    };
    std::deque<DelayedInbound> simLatInbound_;

    /// @brief Send a UDP datagram immediately if the latency simulator is
    /// off, otherwise queue it for delayed send. Caller MUST already hold
    /// `stateMutex_` (matching the existing UDP send call sites).
    /// @return False if the immediate send failed; true otherwise (queued
    ///         sends always optimistically return true — failures surface
    ///         later from the network thread's drain).
    bool sendUdpDelayed(net::PacketHeader hdr, const void* data, int len);

    /// @brief Enqueue an assembled UDP message into `udpRecvQueue_`
    /// immediately if the simulator is off, otherwise hold it in the
    /// inbound delay queue. Caller MUST already hold `stateMutex_`.
    void recvUdpDelayed(std::vector<uint8_t>&& payload);

    /// @brief Network-thread main loop body.
    void networkLoop();

    /// @brief Decode and dispatch a single complete framed message.
    /// Called by poll() after pulling the bytes out of recvBuf.
    void dispatchMessage(const uint8_t* data, Uint32 size);

    /// @brief Invoke snapshotApplyFn_ with raw snapshot bytes; updates delta-decode state on success.
    bool applySnapshot(std::uint32_t snapshotTick, const std::uint8_t* bytes, Uint32 size, Uint32 wireSize);
};
