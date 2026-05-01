/// @file Client.hpp
/// @brief TCP client for connecting to the game server.

#pragma once

#include "ecs/components/InputSnapshot.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/MatchStatus.hpp"
#include "network/MessageStream.hpp"
#include "network/NetKillEvent.hpp"
#include "network/NetworkConfig.hpp"
#include "network/OutboundQueue.hpp"
#include "network/RegistrySerialization.hpp"
#include "network/ShotEvent.hpp"
#include "network/transport/FragmentReassembler.hpp"
#include "network/transport/UdpEndpoint.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <array>
#include <atomic>
#include <deque>
#include <entt/entt.hpp>
#include <mutex>
#include <optional>
#include <random>
#include <thread>

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

/// @brief TCP stream client — sends input to the server and receives state updates.
class Client
{
public:
    using LocalPlayerReadyFn = std::function<void(entt::entity localEntity)>;
    using ParticleEventCallback = std::function<void(const NetParticleEvent& evt, entt::entity localEntity)>;
    using MatchStateUpdateFn = std::function<void(const MatchStatePacket&)>;
    using KillEventCallback = std::function<void(const NetKillEvent&)>;

    /// @brief Create the TCP socket and connect to the server.
    /// @param addr      Hostname or IP address of the server.
    /// @param port      TCP port the server is listening on. The UDP
    ///                  sidecar (Phase 3d) connects to the same port.
    /// @param transport Phase 3d: which UDP features to enable.
    /// @return False on socket creation or DNS failure.
    bool init(const char* addr, Uint16 port, const TransportConfig& transport = {});

    /// @brief Close the socket and release the resolved address.
    void shutdown();

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

    /// @brief Send a PING packet to the server for RTT measurement.
    void sendPing();

    /// @brief Update bandwidth stats. Call once per frame with the frame delta time.
    void updateStats(float dt);

    void onLocalPlayerReady(LocalPlayerReadyFn fn) { localPlayerReadyFn = std::move(fn); }
    void onParticleEvent(ParticleEventCallback fn) { particleEventFn_ = std::move(fn); }
    void onMatchStateUpdate(MatchStateUpdateFn fn) { matchStateUpdateFn_ = std::move(fn); }
    void onKillEvent(KillEventCallback fn) { killEventFn_ = std::move(fn); }

    /// @brief Receive and process one pending message.
    /// @return True if a message was received, false if the queue is empty.
    bool poll(Registry& registry);

    /// @brief Access current network statistics.
    const NetworkStats& getNetStats() const { return stats; }

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
    [[nodiscard]] float getSnapshotAlpha() const;

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
    std::optional<registry_serialization::Loader> registryLoader;
    LocalPlayerReadyFn localPlayerReadyFn;         ///< Called once the server assigns a player entity.
    ParticleEventCallback particleEventFn_;        ///< Called for each replicated particle event from server.
    MatchStateUpdateFn matchStateUpdateFn_;        ///< Called whenever a MATCH_STATE packet is received.
    KillEventCallback killEventFn_;                ///< Called for each replicated kill event from server.
    std::optional<entt::entity> localPlayerEntity; ///< The local player's entity, once assigned by the server.
    bool localPlayerReadyNotified = false;         ///< True if localPlayerReadyFn has been called.

    // ── PR-10 (server-perf): snapshot delta encoding state ──────────────
    //
    // `lastSnapshotPayload_` holds the most-recent FULL snapshot's raw
    // entt-serialized bytes (no PacketType prefix, no tick). When a
    // DELTA packet arrives we apply its patch on top of these bytes
    // to reconstruct the new full state, then feed *that* into the
    // existing Loader. Both bytes and tick are then replaced so
    // subsequent deltas have the right baseline.
    //
    // If `fromTick` on a DELTA doesn't match `lastSnapshotTick_` the
    // packet is silently dropped (the missing piece) — the next
    // periodic full keyframe (every 16 snapshots ≈ 500 ms at 32 Hz)
    // will resync.
    std::vector<uint8_t> lastSnapshotPayload_;
    std::uint32_t lastSnapshotTick_ = 0;

    NetworkStats stats; ///< Live network metrics.

    // Bandwidth tracking — accumulated between updateStats() calls.
    uint64_t bytesSentWindow = 0;
    uint64_t bytesRecvWindow = 0;
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
    // calling sendInputSnapshot / sendPing / poll(registry) — those now
    // touch the queue + recvBuf under stateMutex_ rather than doing
    // syscalls inline. The win is that a render-frame stutter on the game
    // thread no longer causes the kernel buffer to back up.
    OutboundQueue outbound_;
    std::mutex stateMutex_;
    std::thread networkThread_;
    std::atomic<bool> shouldStop_{false};

    /// @brief Latched-true once the network thread observes a socket error.
    /// poll(registry) checks this and reports false to the game thread, so
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
    net::UdpEndpoint udpEndpoint_;
    net::UdpEndpointAddr serverUdpAddr_;
    uint32_t connectionId_ = 0;
    uint16_t udpInputSequence_ = 0; ///< Per-channel sequence for INPUT datagrams.

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
    uint16_t reliableHighestSeen_ = 0;
    uint64_t reliableSeenBitmask_ = 0;
    bool reliableHasAny_ = false; ///< False until the first reliable event arrives.

    /// @brief Sliding-window dedup helper. Returns true if the
    /// caller should dispatch this sequence (i.e. it's new); false
    /// if it's a duplicate or too old to track.
    bool acceptReliableSequence(uint16_t seq);

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
    /// Called by poll(registry) after pulling the bytes out of recvBuf.
    void dispatchMessage(const uint8_t* data, Uint32 size, Registry& registry);
};
