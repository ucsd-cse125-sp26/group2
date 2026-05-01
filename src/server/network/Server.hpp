/// @file Server.hpp
/// @brief TCP game server that accepts clients and dispatches incoming packets.

#pragma once

#include "ecs/components/ClientId.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/MatchStatus.hpp"
#include "network/MessageStream.hpp"
#include "network/NetworkConfig.hpp"
#include "network/OutboundQueue.hpp"
#include "network/ShotEvent.hpp"
#include "network/transport/UdpEndpoint.hpp"
#include "systems/EventQueue.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <atomic>
#include <deque>
#include <entt/entity/entity.hpp>
#include <mutex>
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
    bool init(const char* addr, Uint16 port, const TransportConfig& transport = {});

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

    /// @brief Update client with new entity id.
    /// @return true if sent, otherwise false.
    bool notifyPlayerClientId(ClientId clientId, entt::entity playerEntity);

    /// @brief Broadcast the full registry state to all clients.
    void broadcastRegistry(const Registry& registry);

    /// @brief Broadcast particle events to all clients for effect replication.
    void broadcastParticleEvents(const std::vector<NetParticleEvent>& events);

    /// @brief Get the number of currently connected clients.
    /// @return The client count.
    int getClientCount();

    /// @brief Phase 6: get this client's most-recently-reported smoothed RTT.
    /// @param clientId Network client identifier.
    /// @return RTT in milliseconds, or 0 if the client isn't connected
    ///         or hasn't sent its first INPUT packet yet (which carries
    ///         the RTT field — see the wire format note in
    ///         `Connection::lastReportedRttMs`).
    uint16_t getClientRttMs(ClientId clientId);

    /// @brief Broadcast match status updates to clients.
    void broadcastMatchStatus(MatchStatePacket packet);

    /// @brief Broadcast kill events to clients for kill feed updates.
    void broadcastKillEvents(const std::vector<NetKillEvent>& events);

    /// @brief Drain every connection's outbound queue to its socket.
    ///
    /// Call once per server tick, after all per-tick broadcasts. Disconnects
    /// any client whose socket reports an error during drain.
    ///
    /// Stage 3a: runs on the game thread. Stage 3b moves the actual I/O to a
    /// dedicated network thread; this method's behaviour from the gameplay
    /// layer's perspective stays the same.
    void flushAllOutbound();

private:
    /// @brief Per-client connection state.
    struct Connection
    {
        MessageStream msgStream;    ///< Framed message stream for this client.
        ClientId clientId;          ///< Unique identifier assigned on accept.
        bool pendingInitialization; ///< True if waiting for Game to initialize player entity.

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
        uint32_t connectionId = 0;

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
        struct PendingReliableEvent
        {
            uint16_t sequence;           ///< Per-channel send sequence.
            uint8_t remainingSends;      ///< Decremented each send; popped at 0.
            std::vector<uint8_t> framed; ///< `[PacketType][rest]` payload bytes.
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
    };

    /// @brief Dispatch a single decoded message from a client.
    /// @param client The connection the message arrived on.
    /// @param data   Pointer to the message payload.
    /// @param len    Payload length in bytes.
    void handleMessage(Connection& client, const void* data, Uint32 len);

    /// @brief Accept up to one new client connection per call.
    void acceptClients();

    /// @brief Disconnect a client and clean up resources.
    /// @param conn The client connection to disconnect.
    void disconnectClient(Connection conn);

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
    void handleUdpUnreliable(uint32_t connId, const net::UdpEndpointAddr& from, const uint8_t* payload, uint32_t len);

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
    TransportConfig transportConfig_;
    std::unordered_map<uint32_t, ClientId> connIdToClient_; ///< UDP connection-id → ClientId lookup.

    // ── Stage 3b: dedicated network thread ────────────────────────────────
    //
    // The mutex protects every member above (clients map, eventQueue,
    // nextClientId, NET_Server*) — both the game thread (enqueueTo /
    // enqueueBroadcast / dequeueEvent / notifyPlayerClientId) and the
    // network thread (acceptClients / readClients / flushAllOutbound) hold
    // it during state mutation. Lock holds are kept short (one phase at a
    // time) so the game thread isn't starved.
    std::mutex stateMutex_;
    std::thread networkThread_;
    std::atomic<bool> shouldStop_{false};
};
