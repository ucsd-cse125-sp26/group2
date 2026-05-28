/// @file Server.hpp
/// @brief TCP game server that accepts clients and dispatches incoming packets.

#pragma once

#include "ecs/components/ClientId.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/ChatProtocol.hpp"
#include "network/MatchConfig.hpp"
#include "network/MatchStatus.hpp"
#include "network/MessageStream.hpp"
#include "network/NetworkConfig.hpp"
#include "network/OutboundQueue.hpp"
#include "network/ShotEvent.hpp"
#include "network/VoiceProtocol.hpp"
#include "network/lobby/LobbyStatus.hpp"
#include "network/transport/UdpEndpoint.hpp"
#include "network/transport/UdpSessionTransport.hpp"
#include "systems/EventQueue.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <atomic>
#include <deque>
#include <entt/entity/entity.hpp>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

/// @brief TCP stream socket — receives client packets and echoes them back.
///
/// Call poll() every tick to drain incoming messages.
/// Extend handleMessage() with proper packet dispatch as the game protocol grows.
class Server
{
public:
    /// @brief Bind a TCP socket to the given address and port.
    /// @param addr      Hostname or IP to bind to (e.g. "127.0.0.1").
    /// @param port      TCP port to listen on. The UDP sidecar binds to
    ///                  the same port (different protocol = different
    ///                  socket; OS handles the demux).
    /// @param transport Phase 3d: which UDP features to enable.
    /// @return False on DNS or socket creation failure.
    bool init(const char* addr,
              Uint16 port,
              const TransportConfig& transport = {},
              const GlobalDiscoveryConfig& discovery = {});

    /// @brief Close the socket and release resources.
    void shutdown();

    /// @brief No-op since stage 3b moved I/O onto a dedicated network thread.
    ///
    /// Kept as a public function so existing ServerGame code keeps compiling;
    /// the network thread (started by `init` and stopped by `shutdown`) does
    /// the real work continuously in the background. Safe to call from the
    /// game thread; just doesn't do anything.
    void poll() {}

    /// @brief Check whether the event queue is empty.
    /// @return True if no events are pending.
    bool isEmpty();

    /// @brief Remove and return the next event from the queue.
    /// @return The front event.
    Event dequeueEvent();

    /// @brief PR-2b (server-perf): drain every queued event in FIFO order
    /// into @p out under a single mutex acquisition. Pre-PR-2b the game
    /// thread's tick loop did `isEmpty()`+`dequeueEvent()` per event,
    /// each acquiring the state mutex separately; at 100 bots × 128 Hz
    /// that was the dominant tick scope (12 ms p99). This collapses
    /// the per-event cost to a single lock + swap per tick.
    void drainEvents(std::vector<Event>& out);

    /// @brief Update client with new entity id.
    /// @return true if sent, otherwise false.
    bool notifyPlayerClientId(ClientId clientId, entt::entity playerEntity);

    /// @brief Broadcast the full registry state to all clients.
    void broadcastRegistry(const Registry& registry);

    /// @brief Broadcast particle events to all clients for effect replication.
    void broadcastParticleEvents(const std::vector<NetParticleEvent>& events);

    /// @brief Broadcast lobby status updates to clients.
    void broadcastLobbyUpdate(const LobbyUpdateEvent& event);

    /// @brief Get the number of currently connected clients.
    /// @return The client count.
    int getClientCount();

    /// @brief Directory-assigned global server id, or 0 before registration succeeds.
    uint32_t globalDirectoryServerId() const noexcept;

    /// @brief Phase 6: get this client's most-recently-reported smoothed RTT.
    /// @param clientId Network client identifier.
    /// @return RTT in milliseconds, or 0 if the client isn't connected
    ///         or hasn't sent its first INPUT packet yet (which carries
    ///         the RTT field — see the wire format note in
    ///         `Connection::lastReportedRttMs`).
    uint16_t getClientRttMs(ClientId clientId);

    /// @brief PR-2b (server-perf): bulk-snapshot every connected client's
    /// last-reported network state in one (mostly lock-free) operation.
    ///
    /// PR-12 expanded this from "just RTT" to "RTT + interp-delay" so
    /// the game thread's `updateLagCompTargets` can compute the rewind
    /// formula `targetServerTick = currentServerTick − (RTT/2 + interp)`
    /// from a single snapshot.  At 100 bots × 128 Hz, the per-client
    /// form was ~13 k mutex ops/sec on `stateMutex_`, contending hard
    /// with the network thread; bulk snapshot collapses to 1 op/tick
    /// (PR-4 atomic publish makes the read itself lock-free).
    ///
    /// @param out Cleared and filled with `ClientNetState` records in
    ///            unspecified order. Caller is expected to reuse the
    ///            same vector across ticks to avoid allocation churn.
    struct ClientNetState
    {
        ClientId id;
        uint16_t rttMs;
        uint8_t interpDelaySnapshots;
    };
    void snapshotClientNetStates(std::vector<ClientNetState>& out);

    /// @brief Broadcast match status updates to clients.
    void broadcastMatchStatus(MatchStatePacket packet);

    /// @brief Broadcast kill events to clients for kill feed updates.
    void broadcastKillEvents(const std::vector<NetKillEvent>& events);

    /// @brief Broadcast a sanitized all-chat message from one client.
    void broadcastTextChat(ClientId sender, std::string_view message);

    /// @brief Send one proximity-routed voice frame to a specific listener.
    bool sendVoiceFrameToClient(ClientId recipient,
                                ClientId speaker,
                                std::uint16_t sequence,
                                std::uint8_t frameMs,
                                std::span<const std::uint8_t> opus);

    /// @brief Send one proximity-routed voice frame to multiple listeners.
    bool sendVoiceFrameToClients(std::span<const ClientId> recipients,
                                 ClientId speaker,
                                 std::uint16_t sequence,
                                 std::uint8_t frameMs,
                                 std::span<const std::uint8_t> opus);

    /// @brief PR-20: unicast a serialized SHOT_DEBUG_REPORT (or any
    /// other already-framed payload) to a single client.  Wraps the
    /// private `enqueueTo` so call sites in `ServerGame` can address
    /// the shooter without the broadcast cost paid by every player.
    /// @return False if the client isn't currently connected.
    bool sendToClient(const ClientId& clientId, const void* data, int len);

    /// @brief Unicast a full lobby snapshot to a single client.
    /// @return False if the client isn't currently connected.
    bool sendLobbyStateToClient(ClientId clientId, const std::vector<LobbyPlayer>& players);

    /// @brief Unicast the current match settings to a single client.
    bool sendMatchConfigToClient(ClientId clientId, const MatchConfig& config);

    /// @brief Update the authoritative client admission cap.
    void setMaxPlayers(int maxPlayers);

    /// @brief Enable or disable global directory heartbeats at runtime.
    void setAdvertiseServer(bool enabled);

    /// @brief Drain every connection's outbound queue to its socket.
    ///
    /// Call once per server tick, after all per-tick broadcasts. Disconnects
    /// any client whose socket reports an error during drain.
    ///
    /// Stage 3a: runs on the game thread. Stage 3b moves the actual I/O to a
    /// dedicated network thread; this method's behaviour from the gameplay
    /// layer's perspective stays the same.
    void flushAllOutbound();

    /// @brief Invoked on the network thread immediately after a new client is accepted.
    using ClientConnectedCallback = std::function<void(ClientId)>;
    /// @brief Invoked on the network thread immediately after a client is disconnected.
    using ClientDisconnectedCallback = std::function<void(ClientId)>;
    /// @brief Register a callback fired whenever a client connects.
    void onClientConnected(ClientConnectedCallback fn) { clientConnectedFn_ = std::move(fn); }
    /// @brief Register a callback fired whenever a client disconnects.
    void onClientDisconnected(ClientDisconnectedCallback fn) { clientDisconnectedFn_ = std::move(fn); }

    /// @brief Get the actual port the server is listening on. Useful when the caller requested port 0 (auto-assign) and
    /// needs to know which port was assigned.
    uint16_t listeningPort() const;

    void broadcastMatchConfig(const MatchConfig& config);

private:
    ClientConnectedCallback clientConnectedFn_;       ///< Fired by acceptClients() when a new client is accepted.
    ClientDisconnectedCallback clientDisconnectedFn_; ///< Fired by disconnectClient() when a client drops.

    /// @brief Per-client connection state.
    ///
    /// PR-5b (server-perf): default-initialised for `try_emplace`
    /// in `acceptClients`. Pre-PR-5b the struct was copy-constructed
    /// from a designated-initialiser temporary, which would no
    /// longer compile once `OutboundQueue` grew an internal mutex.
    struct Connection
    {
        MessageStream msgStream{};         ///< Framed message stream for this client.
        ClientId clientId{};               ///< Unique identifier assigned on accept.
        bool pendingInitialization = true; ///< True if waiting for Game to initialize player entity.

        /// @brief Highest InputSnapshot.tick this client has had applied to the
        /// simulation. Used to dedup multi-input redundancy: each client sends
        /// the last N inputs every tick, so most arrivals are duplicates of
        /// already-applied data. We accept only inputs strictly newer than this
        /// value and update it as we process. Resets to 0 on reconnect because
        /// each Connection is constructed fresh.
        uint32_t lastAppliedInputTick = 0;

        /// @brief Per-client userspace outbound queue (Phase 3a).
        ///
        /// All broadcast helpers push into this queue; the queue is then
        /// flushed once at end-of-tick via `flushAllOutbound()`. The
        /// replace-on-stale semantics mean a slow drainer only ever has one
        /// pending UPDATE_REGISTRY in flight (always the freshest), instead
        /// of accumulating dozens of obsolete snapshots in SDL3_net's
        /// internal pending_output_buffer.
        OutboundQueue outbound;

        /// @brief Phase 3d: server-assigned UDP connection ID.
        ///
        /// Generated when the TCP connection is accepted and shipped to
        /// the client in the ASSIGN_CLIENT_ID packet. Clients stamp every
        /// outbound UDP datagram with this; the server demuxes incoming
        /// UDP via `connIdToClient_` to find which TCP-established client
        /// the datagram is from. 0 = not yet assigned.
        std::uint64_t connectionId = 0;

        /// @brief Phase 3d: source address of the most-recent UDP packet
        /// from this client. Filled in lazily on first UDP receive (the
        /// client's actual UDP source port isn't known until then —
        /// it's auto-assigned by their kernel). Server uses this to
        /// route UDP replies (PONG, future server→client UDP traffic).
        net::UdpEndpointAddr udpAddr;

        /// @brief Phase 3d-4: per-client outgoing sequence for the
        /// Unreliable channel's snapshot stream. Increments on every
        /// snapshot the server sends to this client. Receiver uses it
        /// to drop stale fragments when a newer set arrives.
        uint16_t udpSnapshotSequence = 0;

        /// @brief Phase 3d-5: per-client outgoing sequence for the
        /// reliable event stream over UDP. Each pushed event gets
        /// next-sequence; client dedups against a sliding-window
        /// bitset of recently-seen sequences.
        uint16_t reliableNextSequence = 0;

        /// @brief Phase 3d-5: pending reliable events, each scheduled
        /// for `remainingSends` more cycles. Server drains in network
        /// loop; entries with `remainingSends == 0` are popped.
        ///
        /// PR-5a (server-perf): `framed` is now `shared_ptr<const ...>`
        /// so a single broadcast (kill, particle, match-state) shares
        /// one byte buffer across N clients. Pre-PR-5a each client got
        /// a fresh `std::vector<uint8_t>` copy — at 300 clients × 12.5 %
        /// fire-burst ticks that was the visible `broadcastEvents`
        /// p99 spike at 25–50 ms.
        struct PendingReliableEvent
        {
            uint16_t sequence;                                  ///< Per-channel send sequence.
            uint8_t remainingSends;                             ///< Decremented each send; popped at 0.
            std::shared_ptr<const std::vector<uint8_t>> framed; ///< `[PacketType][rest]` payload bytes (shared).
        };
        std::deque<PendingReliableEvent> reliableQueue;

        /// @brief Phase 6: client's most-recent self-reported smoothed
        /// RTT in milliseconds. Updated on every INPUT packet (the
        /// 2-byte `rttMs` prefix in the wire format). Read by the
        /// lag-compensation scheduler each server tick to size this
        /// client's hitscan rewind window — `targetServerTick =
        /// currentServerTick - clamp(rttMs/2 → ticks, 0, k_maxLagCompTicks)`.
        /// Stays at 0 until the first INPUT packet arrives, which
        /// means brand-new connections start with no rewind (correct:
        /// their PING/PONG hasn't completed yet so any rewind would
        /// be guesswork).
        uint16_t lastReportedRttMs = 0;

        /// @brief PR-12: client's most-recent self-reported render-delay
        /// in *snapshots* (i.e. their `cl_interp` value).  The PR-11
        /// renderer plays back at `now − N × snapshotInterval` for some
        /// client-chosen N (default 2).  Lag comp must include this
        /// term in its rewind so the server hitbox state lines up with
        /// what the client SAW when they pulled the trigger — not
        /// merely with what the server held at INPUT-arrival time.
        ///
        /// New rewind formula:
        ///   `targetServerTick = currentServerTick
        ///                       − clamp(rttHalfTicks + interpDelayTicks,
        ///                               0, k_maxLagCompTicks)`
        ///
        /// where `interpDelayTicks = N × (tickRateHz / snapshotRateHz)`.
        /// At default 32 Hz snapshots / 128 Hz physics, N=2 → 8 ticks
        /// (~62.5 ms).  Clamped to InterpolationBuffer::k_capacity (8)
        /// on the wire, so the worst case is 8 snapshots × 4 ticks =
        /// 32 ticks (250 ms) of interp on top of RTT/2.
        ///
        /// Stays at 0 until the first INPUT packet arrives.  Source
        /// engine ships this value over the wire as well — see TF2
        /// `cl_interp` / Quake `cl_interp_ratio`.
        uint8_t lastReportedInterpDelaySnapshots = 0;

        Uint64 chatWindowStartMs = 0;
        std::uint8_t chatMessagesInWindow = 0;
        Uint64 voiceWindowStartMs = 0;
        std::uint8_t voiceFramesInWindow = 0;
    };

    /// @brief Dispatch a single decoded message from a client.
    /// @param client The connection the message arrived on.
    /// @param data   Pointer to the message payload.
    /// @param len    Payload length in bytes.
    void handleMessage(Connection& client, const void* data, Uint32 len);

    /// @brief Accept up to one new client connection per call.
    /// @return The newly connected ClientId, or ClientId{} (value -1) if none.
    ClientId acceptClients();

    /// @brief Disconnect a client and clean up resources.
    ///
    /// PR-5b (server-perf): now takes a reference. Pre-PR-5b it
    /// took `Connection` by value, which was a quiet per-disconnect
    /// std::vector<...> + per-deque copy. Once Connection grew an
    /// internal `std::mutex` (in OutboundQueue) it stopped being
    /// copyable and the by-value form would no longer compile —
    /// switching to a reference is both faster and required.
    /// The function reads only fields it doesn't mutate beyond the
    /// final socket-destroy + udpAddr release; the caller is
    /// expected to erase the entry from `clients` after.
    void disconnectClient(Connection& conn);

    /// @brief Read and process pending messages from all connected clients.
    void readClients();

    /// @brief Generate next unique client ID
    ClientId getNextClientId();

    /// @brief Enqueue raw data for one client.
    /// @param replaceKey  See OutboundEntry::replaceKey (0 = always append,
    ///                    non-zero = replace existing entry with same key).
    /// @return False if the client is not connected.
    bool enqueueTo(const ClientId& clientId, uint8_t replaceKey, const void* data, int len);

    /// @brief Enqueue raw data for all currently-connected clients.
    /// @param replaceKey See `enqueueTo`.
    void enqueueBroadcast(uint8_t replaceKey, const void* data, int len);

    /// @brief Phase 3d-5: enqueue a reliable event for all clients.
    ///
    /// The event is pushed to each client's `reliableQueue` with a
    /// fresh per-client sequence and `k_reliableRedundancy` send
    /// budget. Network loop ships entries via UDP each cycle and
    /// decrements the budget; entries with budget==0 get popped.
    /// Falls back to TCP `OutboundQueue` (with replaceKey=0) when
    /// the events-over-udp toggle is off, so the same broadcast
    /// helpers work in both modes.
    void enqueueReliableEvent(const void* data, int len);

    /// @brief Network-thread main loop body.
    ///
    /// Runs continuously between `init` and `shutdown`, taking the mutex
    /// briefly for each I/O phase so the game thread isn't starved while
    /// (e.g.) draining 100 client outbound queues. Uses `SDL_Delay(1)`
    /// between cycles for a ~1 kHz tick — fast enough that game-thread
    /// enqueues turn into wire bytes within a millisecond, slow enough not
    /// to burn a full core.
    void networkLoop();

    /// @brief Dispatch a UDP datagram received with channel == Unreliable.
    ///
    /// Reads the first byte of @p payload as a PacketType discriminator
    /// (mirrors the TCP wire format) and routes to the appropriate
    /// handler. Currently handles INPUT (Phase 3d-2) and PING (3d-3).
    /// All others are dropped silently — they're either meant for TCP
    /// or not yet ported to UDP.
    void
    handleUdpUnreliable(std::uint64_t connId, const net::UdpEndpointAddr& from, const uint8_t* payload, uint32_t len);
    void
    handleSessionPayload(std::uint64_t connId, net::ChannelId channel, const std::uint8_t* payload, std::uint32_t len);
    void handleVoiceFrame(Connection& conn, const std::uint8_t* payload, std::uint32_t len);
    void handleDirectoryEvent(const std::vector<std::uint8_t>& payload, const net::UdpEndpointAddr& from);
    void sendDirectoryHeartbeat(Uint64 nowMs);

    NET_Server* server = nullptr;                     ///< Underlying SDL_net server handle.

    std::unordered_map<ClientId, Connection> clients; ///< Currently connected clients.
    EventQueue eventQueue;                            ///< Incoming events awaiting processing.

    ClientId nextClientId;                            ///< Counter for assigning client IDs.

    // ── Phase 3d: UDP sidecar ─────────────────────────────────────────────
    //
    // Bound during init() to the same port as the TCP listener (different
    // protocol = different socket; OS demuxes). The network thread polls
    // both. Server-assigned 32-bit connection IDs let us match an
    // incoming UDP datagram to a TCP-established Connection — the IDs
    // are minted on TCP accept and shipped to the client in the
    // ASSIGN_CLIENT_ID packet so it can stamp them on outbound UDP.
    net::UdpEndpoint udpEndpoint_;
    net::UdpSessionTransport session_;
    TransportConfig transportConfig_;
    GlobalDiscoveryConfig discoveryConfig_;
    net::UdpEndpointAddr directoryAddr_;
    std::atomic<std::uint32_t> directoryServerId_{0};
    Uint64 lastDirectoryHeartbeatMs_ = 0;
    std::uint32_t nextChatServerSeq_ = 0;
    std::atomic<int> maxPlayers_{128};
    std::atomic<bool> advertiseServer_{true}; ///< Runtime global-directory heartbeat toggle.
    bool usingUdpSession_ = false;
    Uint16 listenPort_ = 0;
    std::unordered_map<std::uint64_t, ClientId> connIdToClient_; ///< UDP connection-id → ClientId lookup.

    // ── Stage 3b: dedicated network thread ────────────────────────────────
    //
    // The mutex protects every member above (clients map, eventQueue,
    // nextClientId, NET_Server*) — both the game thread (enqueueTo /
    // enqueueBroadcast / dequeueEvent / notifyPlayerClientId) and the
    // network thread (acceptClients / readClients / flushAllOutbound) hold
    // it during state mutation. Lock holds are kept short (one phase at a
    // time) so the game thread isn't starved.
    // PR-6 (server-perf): shared_mutex so read-mostly paths
    // (enqueueBroadcast iterating clients, flushAllOutbound's phase 1
    // snapshot, readClients's per-client poll, UDP receive) can take
    // shared locks and run concurrently. Writers (acceptClients,
    // disconnect-application, reliable-queue mutation) take unique.
    std::shared_mutex stateMutex_;
    std::thread networkThread_;
    std::atomic<bool> shouldStop_{false};

    // ── PR-2 (server-perf): deferred snapshot fanout ──────────────────────
    //
    // `broadcastRegistry` no longer does the per-client send loop on the
    // game thread. Instead it serializes the snapshot once, wraps the
    // bytes in `shared_ptr<const ...>`, and stores both the unframed
    // payload (for the UDP path's per-fragment send loop) and the
    // length-prefixed framed bytes (for the TCP fallback's enqueue) in
    // these slots. The network thread picks them up at the start of its
    // next cycle and runs the fanout there.
    //
    // Why two buffers: the UDP path expects the raw payload (the
    // PacketHeader is added per-fragment); the TCP path expects the
    // 4-byte length prefix already prepended (it goes straight into
    // OutboundQueue → NET_WriteToStreamSocket). Pre-PR-2 the framing was
    // done per client; now it's once per snapshot and shared.
    //
    // Both pointers protected by stateMutex_; the network thread
    // exchanges to nullptr atomically under the lock so a fresh snapshot
    // arriving mid-cycle replaces the old one without races.
    std::shared_ptr<const std::vector<uint8_t>> pendingSnapshotPayload_;
    std::shared_ptr<const std::vector<uint8_t>> pendingSnapshotFramed_;

    // ── PR-10 (server-perf): snapshot delta encoding state ────────────────
    //
    // `prevSnapshotRaw_` is the *unprefixed* `registry_serialization::
    // serialize()` output from the previous broadcastRegistry call —
    // i.e. NOT the wire-prefixed form, just the entt-snapshot bytes.
    // We diff the next call's serialize() against this; if the patch
    // is at least 25% smaller than a full payload AND the size matches
    // (entity count unchanged), we ship a DELTA packet referencing
    // `prevSnapshotTick_`. Clients drop the DELTA if their last-applied
    // tick != `prevSnapshotTick_` — they wait for the next FULL.
    //
    // `snapshotCounter_` increments each broadcastRegistry call. Every
    // `k_keyframeInterval`-th call is forced to FULL regardless of
    // delta size, so clients that fell behind one delta still resync
    // within ≤ 500 ms (at 32 Hz × 16 = 500 ms).
    //
    // PR-14 (loss resilience): `keyframeRaw_` / `keyframeTick_` now
    // hold the most recent FULL keyframe, NOT the immediately previous
    // snapshot.  Every delta within a keyframe window is encoded
    // against the same fixed baseline, so a single dropped delta no
    // longer cascades — the next-arriving delta still decodes against
    // the keyframe the client holds.  The keyframe is replaced only
    // when a new FULL is sent (forced by `k_keyframeInterval` or
    // size-change fallback).  `snapshotCounter_` tracks the absolute
    // snapshot number for tick stamping and keyframe scheduling.
    //
    // All three fields are touched only on the game thread inside
    // broadcastRegistry — no synchronisation needed.
    std::vector<uint8_t> keyframeRaw_;
    std::uint32_t keyframeTick_ = 0;
    std::uint32_t snapshotCounter_ = 0;

    // ── PR-4 (server-perf): atomic-published read snapshots ───────────────
    //
    // Pre-PR-4 the game-thread query path (snapshotClientRtts,
    // getClientCount, broadcastMatchStatus's getClientCount) acquired
    // `stateMutex_` once per call and competed with the network thread's
    // long-running readClients() pass. At 200+ bots that lock-wait was
    // the dominant tick-time spike (50 ms p99 on lagcompTargets, 25 ms
    // on match) — see §9 of docs/server-perf-design.md.
    //
    // PR-4 publishes both a per-client RTT snapshot and a client-count
    // gauge atomically from the network thread. The game thread reads
    // them lock-free. Trade-offs:
    //   - Snapshot is at most one network-thread cycle (~1 ms) stale.
    //     For lag-comp's RTT/2 rewind window that's negligible.
    //   - PR-30 (cross-platform): we use the C++11 free-function API
    //     `std::atomic_load_explicit(&shared_ptr, …)` /
    //     `std::atomic_store_explicit(&shared_ptr, …)` rather than the
    //     C++20 `std::atomic<std::shared_ptr<T>>` partial specialization.
    //     Reason: libstdc++ 12+ ships the C++20 specialization, but
    //     Apple's libc++ (through Xcode 16 / libc++ 19) does NOT.
    //     The free-function API is deprecated in C++20 but remains
    //     available through C++23 across all stdlibs we ship to, and
    //     lowers to the same lock-free atomic ops on x86/ARM.  Local
    //     pragma suppression in `Server.cpp` silences the deprecation
    //     warnings at the call sites.
    /// @brief Atomic-published snapshot of every connected client's
    /// network state.  PR-12 extended this from `(id, rtt)` pairs to
    /// `(id, rtt, interpDelaySnapshots)` so a single tick of
    /// `updateLagCompTargets` can read both terms of the rewind
    /// formula in one lock-free fetch.
    struct ClientRttSnapshot
    {
        std::vector<ClientNetState> entries;
    };
    std::shared_ptr<const ClientRttSnapshot> rttSnapshotAtomic_;
    std::atomic<std::uint32_t> clientCountAtomic_{0};
};
