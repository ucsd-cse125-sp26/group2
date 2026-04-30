/// @file Server.hpp
/// @brief TCP game server that accepts clients and dispatches incoming packets.

#pragma once

#include "ecs/components/ClientId.hpp"
#include "ecs/registry/Registry.hpp"
#include "ecs/systems/PlayerStatusSystem.hpp"
#include "network/MatchStatus.hpp"
#include "network/MessageStream.hpp"
#include "network/OutboundQueue.hpp"
#include "network/ShotEvent.hpp"
#include "systems/EventQueue.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <entt/entity/entity.hpp>
#include <vector>

/// @brief TCP stream socket — receives client packets and echoes them back.
///
/// Call poll() every tick to drain incoming messages.
/// Extend handleMessage() with proper packet dispatch as the game protocol grows.
class Server
{
public:
    /// @brief Bind a TCP socket to the given address and port.
    /// @param addr  Hostname or IP to bind to (e.g. "127.0.0.1").
    /// @param port  TCP port to listen on.
    /// @return False on DNS or socket creation failure.
    bool init(const char* addr, Uint16 port);

    /// @brief Close the socket and release resources.
    void shutdown();

    /// @brief Drain all pending messages for this tick.
    void poll();

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

    NET_Server* server = nullptr;                     ///< Underlying SDL_net server handle.

    std::unordered_map<ClientId, Connection> clients; ///< Currently connected clients.
    EventQueue eventQueue;                            ///< Incoming events awaiting processing.

    ClientId nextClientId;                            ///< Counter for assigning client IDs.
};
