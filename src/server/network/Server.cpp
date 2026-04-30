/// @file Server.cpp
/// @brief Implementation of the TCP game server.

#include "Server.hpp"

#include "ecs/components/ClientId.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "network/MatchStatus.hpp"
#include "network/PacketType.hpp"
#include "network/RegistrySerialization.hpp"
#include "systems/EventQueue.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <cstring>
#include <entt/entity/entity.hpp>

bool Server::init(const char* addr, Uint16 port)
{
    NET_Address* netAddr = NET_ResolveHostname(addr);
    if (NET_WaitUntilResolved(netAddr, -1) == NET_FAILURE) {
        SDL_Log("Server: failed to resolve address: %s", SDL_GetError());
        NET_UnrefAddress(netAddr);
        return false;
    }

    server = NET_CreateServer(netAddr, port);
    NET_UnrefAddress(netAddr);
    if (!server) {
        SDL_Log("Server: failed to create server: %s", SDL_GetError());
        return false;
    }

    eventQueue = EventQueue();
    SDL_Log("Server: listening on port %d", static_cast<int>(port));

    nextClientId.value = 0;

    // ── Stage 3b: spawn the dedicated network thread. ────────────────────
    //
    // Game thread continues to call broadcast helpers and dequeueEvent;
    // those are now guarded by stateMutex_ and the network thread does the
    // actual TCP I/O in the background. Order matters: spawn last so all
    // server state (especially the NET_Server*) is fully initialised
    // before the thread starts touching it.
    shouldStop_.store(false, std::memory_order_relaxed);
    networkThread_ = std::thread(&Server::networkLoop, this);

    return true;
}

void Server::shutdown()
{
    // Tell the network thread to stop and join before tearing down state —
    // otherwise it might be mid-poll on a socket we're about to destroy.
    shouldStop_.store(true, std::memory_order_relaxed);
    if (networkThread_.joinable()) {
        networkThread_.join();
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (server) {
        SDL_Log("Server: shutting down");
        NET_DestroyServer(server);
        server = nullptr;
    }
    for (auto& [_, client] : clients) {
        NET_DestroyStreamSocket(client.msgStream.socket);
    }
    clients.clear();
}

// ── Phase 3a: framing helper ─────────────────────────────────────────────
//
// Build the on-the-wire byte sequence (4-byte length prefix + payload) for a
// single message. Returned by-value so the caller can hand the std::vector
// straight to OutboundQueue::enqueue without further allocation. Done once
// per logical message and shared across every per-client enqueue in a
// broadcast — saves N - 1 framing copies on a broadcast to N clients.
namespace
{
std::vector<uint8_t> frameMessage(const void* data, int len)
{
    std::vector<uint8_t> framed(sizeof(Uint32) + static_cast<size_t>(len));
    const auto msgLen = static_cast<Uint32>(len);
    std::memcpy(framed.data(), &msgLen, sizeof(msgLen));
    std::memcpy(framed.data() + sizeof(msgLen), data, static_cast<size_t>(len));
    return framed;
}
} // namespace

bool Server::enqueueTo(const ClientId& clientId, uint8_t replaceKey, const void* data, int len)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = clients.find(clientId);
    if (it == clients.end())
        return false;

    it->second.outbound.enqueue(replaceKey, frameMessage(data, len));
    return true;
}

void Server::enqueueBroadcast(uint8_t replaceKey, const void* data, int len)
{
    // Frame the message once outside the lock — the framing copy doesn't
    // touch any shared state — then lock briefly only to push per-client
    // copies into each queue.
    auto framed = frameMessage(data, len);

    std::lock_guard<std::mutex> lock(stateMutex_);
    for (auto& [_, conn] : clients) {
        conn.outbound.enqueue(replaceKey, std::vector<uint8_t>(framed));
    }
}

void Server::flushAllOutbound()
{
    // Per-tick max-age for unreliable entries. Anything older than this is
    // dropped before going on the wire — see OutboundQueue::flushTo.
    constexpr Uint32 k_maxAgeMs = 300;

    // Caller (networkLoop or shutdown path) holds stateMutex_.
    for (auto it = clients.begin(); it != clients.end();) {
        auto& conn = it->second;
        if (!conn.outbound.flushTo(conn.msgStream.socket, k_maxAgeMs)) {
            // Socket error during flush — disconnect this client.
            disconnectClient(conn);
            it = clients.erase(it);
            continue;
        }
        ++it;
    }
}

void Server::networkLoop()
{
    // The three I/O phases run separately so the lock can be released
    // between them — letting the game thread enqueue / dequeue without
    // having to wait for an entire I/O cycle to finish.
    while (!shouldStop_.load(std::memory_order_relaxed)) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            acceptClients();
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            readClients();
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            flushAllOutbound();
        }

        // 1 ms cycle: bytes from a game-thread enqueue land on the wire
        // within ~1 ms worst case, but the thread doesn't burn a core
        // hot-spinning. SDL_Delay's resolution is ms; this is a coarse
        // upper bound on outbound latency.
        SDL_Delay(1);
    }
}

void Server::acceptClients()
{
    // Accept up to one new client per tick, should be good enough.

    NET_StreamSocket* socket = nullptr;
    if (!NET_AcceptClient(server, &socket)) {
        SDL_Log("NET_AcceptClient failed: %s", SDL_GetError());
        return;
    } else if (socket) {
        SDL_Log("Server: accepted new client");
        NET_SetStreamSocketNoDelay(socket, true);
        ClientId clientId = getNextClientId();
        clients.insert(
            {clientId,
             Connection{.msgStream = MessageStream(socket), .clientId = clientId, .pendingInitialization = true}});
        eventQueue.enqueue(Event{.clientId = clientId, .type = EventType::Connected, .movementIntent = {}});
    }
}

void Server::disconnectClient(Connection conn)
{
    SDL_Log("Server: disconnecting client %d", conn.clientId.value);
    NET_DestroyStreamSocket(conn.msgStream.socket);
    eventQueue.enqueue(Event{.clientId = conn.clientId, .type = EventType::Disconnected});
}

void Server::readClients()
{
    // packet format is 4 byte length prefix
    for (auto it = clients.begin(); it != clients.end();) {
        auto& conn = it->second;

        bool ok =
            conn.msgStream.poll([this, &conn](const void* data, Uint32 size) { handleMessage(conn, data, size); });

        if (!ok) {
            disconnectClient(conn);
            it = clients.erase(it);
            continue;
        }

        ++it;
    }
}

void Server::handleMessage(Connection& conn, const void* data, Uint32 len)
{
    if (len < 1)
        return;

    auto type = static_cast<PacketType>(reinterpret_cast<const Uint8*>(data)[0]);
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(data) + 1;
    uint32_t payloadLen = len - 1;

    switch (type) {
    case PacketType::INPUT: {
        // New multi-input wire format: [count u8] [InputSnapshot * count],
        // oldest-first. Each client packet carries the last
        // Client::k_inputRedundancy inputs. We dedup against
        // conn.lastAppliedInputTick — most entries in any given packet are
        // duplicates of already-applied snapshots and get skipped cheaply.
        if (payloadLen < 1) {
            SDL_Log("Server: received empty INPUT packet from client %d", conn.clientId.value);
            return;
        }

        const uint8_t count = payload[0];
        // Cap at a sane upper bound to defend against malformed/hostile
        // packets. Keep the cap loose enough that a future bump in
        // Client::k_inputRedundancy doesn't require a server change.
        constexpr uint8_t k_maxInputsPerPacket = 16;
        if (count == 0 || count > k_maxInputsPerPacket) {
            SDL_Log("Server: received INPUT packet with bad count %u from client %d",
                    static_cast<unsigned>(count),
                    conn.clientId.value);
            return;
        }

        const uint32_t expectedSize = 1u + static_cast<uint32_t>(count) * sizeof(InputSnapshot);
        if (payloadLen != expectedSize) {
            SDL_Log("Server: received INPUT packet of invalid size %u (expected %u for count %u)",
                    payloadLen,
                    expectedSize,
                    static_cast<unsigned>(count));
            return;
        }

        const uint8_t* base = payload + 1;
        for (uint8_t i = 0; i < count; ++i) {
            InputSnapshot snap{};
            std::memcpy(&snap, base + i * sizeof(InputSnapshot), sizeof(InputSnapshot));

            // Dedup: skip already-applied or out-of-order-old. The client
            // sends inputs oldest-first so we will normally walk a small
            // run of duplicates and then a small run of fresh inputs.
            if (snap.tick <= conn.lastAppliedInputTick)
                continue;

            Event event{};
            event.movementIntent = snap;
            event.type = EventType::Input;
            event.clientId = conn.clientId;
            eventQueue.enqueue(event);

            conn.lastAppliedInputTick = snap.tick;
        }
        break;
    }

    case PacketType::PING: {
        // Echo the payload back as a PONG so the client can measure RTT.
        // We're called from readClients() which already holds stateMutex_,
        // so push directly to conn.outbound rather than calling enqueueTo
        // (which would re-acquire the same non-recursive mutex and
        // deadlock).  PONG is tiny and one-per-second; replaceKey=0 means
        // "always send, never drop on age".
        uint8_t buf[1 + sizeof(Uint64)];
        buf[0] = static_cast<uint8_t>(PacketType::PONG);
        if (payloadLen == sizeof(Uint64)) {
            std::memcpy(buf + 1, payload, sizeof(Uint64));
            conn.outbound.enqueue(0, frameMessage(buf, static_cast<int>(sizeof(buf))));
        }
        break;
    }

    default:
        SDL_Log("Server: received unknown packet type %d", static_cast<int>(type));
        break;
    }
}

ClientId Server::getNextClientId()
{
    ClientId id = nextClientId;
    nextClientId.value++;
    return id;
}

bool Server::isEmpty()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return eventQueue.isEmpty();
}

Event Server::dequeueEvent()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return eventQueue.dequeue();
}

// NOTE: playerEntity is the entity id of the player
bool Server::notifyPlayerClientId(ClientId clientId, entt::entity playerEntity)
{
    uint8_t buf[1 + sizeof(entt::entity)];
    buf[0] = static_cast<uint8_t>(PacketType::ASSIGN_CLIENT_ID);
    std::memcpy(buf + 1, &playerEntity, sizeof(entt::entity));

    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = clients.find(clientId);
    if (it == clients.end())
        return false;

    // Inlined enqueue under the lock we already hold (calling enqueueTo
    // would re-lock and deadlock on a non-recursive mutex). ASSIGN_CLIENT_ID
    // must be delivered (replaceKey=0 → never dropped on age).
    it->second.outbound.enqueue(0, frameMessage(buf, sizeof(buf)));
    it->second.pendingInitialization = false;
    return true;
}

void Server::broadcastRegistry(const Registry& registry)
{
    auto buf = registry_serialization::serialize(registry);

    buf.insert(buf.begin(), static_cast<uint8_t>(PacketType::UPDATE_REGISTRY));

    // Replace-on-stale: a slow client only ever has one snapshot pending in
    // its userspace queue — always the freshest. Without this, a 100-bot
    // server can stack dozens of obsolete snapshots in a slow drainer's
    // SDL3_net pending_output_buffer, all of which still have to be sent
    // and decoded before catching up.
    enqueueBroadcast(static_cast<uint8_t>(PacketType::UPDATE_REGISTRY), buf.data(), static_cast<int>(buf.size()));
}

void Server::broadcastParticleEvents(const std::vector<NetParticleEvent>& events)
{
    if (events.empty())
        return;

    // Pack: [PacketType::PARTICLE_SPAWN (1B)] [count (4B)] [NetParticleEvent * count]
    const auto count = static_cast<uint32_t>(events.size());
    const size_t payloadSize = 1 + sizeof(uint32_t) + count * sizeof(NetParticleEvent);
    std::vector<uint8_t> buf(payloadSize);

    buf[0] = static_cast<uint8_t>(PacketType::PARTICLE_SPAWN);
    std::memcpy(buf.data() + 1, &count, sizeof(uint32_t));
    std::memcpy(buf.data() + 1 + sizeof(uint32_t), events.data(), count * sizeof(NetParticleEvent));

    // Particle events are discrete moments — each one represents a specific
    // VFX trigger. We don't replace; we append. (replaceKey = 0 → no
    // replace, no age cull → must ship.) Phase 4 will move these to the
    // proper UnreliableSequenced channel where drop-old is OK.
    enqueueBroadcast(0, buf.data(), static_cast<int>(buf.size()));
}

int Server::getClientCount()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return static_cast<int>(clients.size());
}

void Server::broadcastMatchStatus(MatchStatePacket packet)
{
    std::vector<uint8_t> buf(sizeof(PacketType) + sizeof(MatchStatePacket));
    buf[0] = static_cast<uint8_t>(PacketType::MATCH_STATE);
    std::memcpy(buf.data() + 1, &packet, sizeof(MatchStatePacket));

    // MATCH_STATE is small and rare, but a stale phase-transition message is
    // worse than a fresh one — replace.
    enqueueBroadcast(static_cast<uint8_t>(PacketType::MATCH_STATE), buf.data(), static_cast<int>(buf.size()));
}

void Server::broadcastKillEvents(const std::vector<NetKillEvent>& events)
{
    if (events.empty())
        return;

    // Pack: [PacketType::KILL_EVENT (1B)] [count (4B)] [NetKillEvent * count]
    const auto count = static_cast<uint32_t>(events.size());
    const size_t payloadSize = 1 + sizeof(uint32_t) + count * sizeof(NetKillEvent);
    std::vector<uint8_t> buf(payloadSize);

    buf[0] = static_cast<uint8_t>(PacketType::KILL_EVENT);
    std::memcpy(buf.data() + 1, &count, sizeof(uint32_t));
    std::memcpy(buf.data() + 1 + sizeof(uint32_t), events.data(), count * sizeof(NetKillEvent));

    // Kill events must ship (kill-feed correctness). Append, never drop.
    enqueueBroadcast(0, buf.data(), static_cast<int>(buf.size()));

    SDL_Log("Server: broadcasted %u kill events to clients", count);
}
