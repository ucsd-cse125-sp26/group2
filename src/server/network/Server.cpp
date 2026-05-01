/// @file Server.cpp
/// @brief Implementation of the TCP game server.

#include "Server.hpp"

#include "ecs/components/ClientId.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "network/MatchStatus.hpp"
#include "network/PacketType.hpp"
#include "network/RegistrySerialization.hpp"
#include "network/transport/PacketHeader.hpp"
#include "systems/EventQueue.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <cstring>
#include <entt/entity/entity.hpp>
#include <random>

bool Server::init(const char* addr, Uint16 port, const TransportConfig& transport)
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
    transportConfig_ = transport;

    // ── Phase 3d-1: open UDP sidecar on the same port ────────────────────
    //
    // TCP and UDP are different protocols so they can share a port number
    // without collision. The OS demuxes by IP-protocol field. If the UDP
    // bind fails (rare — OS usually only fails this if the port is in use
    // *as UDP* by another app), we log and continue with TCP-only; the
    // 3d-2/3 features fall back transparently when the sidecar isn't open.
    if (transportConfig_.enableUdpSidecar) {
        if (udpEndpoint_.open(addr, port)) {
            SDL_Log("Server: UDP sidecar bound to port %d", static_cast<int>(port));
        } else {
            SDL_Log("Server: UDP sidecar bind failed; falling back to TCP-only");
        }
    }

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
    udpEndpoint_.close();
    connIdToClient_.clear();
    for (auto& [_, client] : clients) {
        NET_DestroyStreamSocket(client.msgStream.socket);
        client.udpAddr.release();
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

void Server::enqueueReliableEvent(const void* data, int len)
{
    // Phase 3d-5: build the framed payload once, then push it into
    // every connected client's reliable queue with a fresh per-client
    // sequence number. Each entry rides UDP `k_reliableRedundancy`
    // times for resilience against UDP loss. If `eventsOverUdp` is
    // off, fall back to the existing TCP path.
    constexpr uint8_t k_reliableRedundancy = 3;
    auto framed = std::vector<uint8_t>(static_cast<size_t>(len));
    std::memcpy(framed.data(), data, static_cast<size_t>(len));

    std::lock_guard<std::mutex> lock(stateMutex_);

    if (!transportConfig_.eventsOverUdp || !udpEndpoint_.isOpen()) {
        // TCP fallback. replaceKey = 0 → never drop on age, always ship.
        for (auto& [_, conn] : clients) {
            conn.outbound.enqueue(0, frameMessage(data, len));
        }
        return;
    }

    for (auto& [_, conn] : clients) {
        conn.reliableQueue.push_back(Connection::PendingReliableEvent{
            .sequence = conn.reliableNextSequence++,
            .remainingSends = k_reliableRedundancy,
            .framed = framed,
        });
    }
}

void Server::handleUdpUnreliable(uint32_t connId,
                                 const net::UdpEndpointAddr& from,
                                 const uint8_t* payload,
                                 uint32_t len)
{
    // Caller (networkLoop's UDP phase) holds stateMutex_.
    auto idIt = connIdToClient_.find(connId);
    if (idIt == connIdToClient_.end())
        return; // unknown connection ID — silently drop

    auto connIt = clients.find(idIt->second);
    if (connIt == clients.end())
        return; // client gone (race with disconnect)
    auto& conn = connIt->second;

    // Cache the source address for server→client UDP replies (PONG, etc).
    // Refresh if we don't already have it or if the source port changed
    // (NAT rebind, client reconnect with new local port, etc.).
    if (conn.udpAddr.addr == nullptr || conn.udpAddr.port != from.port) {
        conn.udpAddr.release();
        conn.udpAddr.addr = NET_RefAddress(from.addr);
        conn.udpAddr.port = from.port;
    }

    // Wire format mirrors TCP: `[PacketType (1B)][rest]`. Channel ID
    // selects reliability semantics; first byte still discriminates the
    // application-layer packet type.
    if (len < 1)
        return;
    const auto type = static_cast<PacketType>(payload[0]);
    const uint8_t* sub = payload + 1;
    const uint32_t subLen = len - 1;

    switch (type) {
    case PacketType::INPUT: {
        // Same parser as the TCP INPUT case. Wire format:
        //   [count u8] [rttMs u16] [InputSnapshot * count]
        if (subLen < 3)
            return;
        const uint8_t count = sub[0];
        constexpr uint8_t k_maxInputsPerPacket = 16;
        if (count == 0 || count > k_maxInputsPerPacket)
            return;
        const uint32_t expectedSize = 3u + static_cast<uint32_t>(count) * sizeof(InputSnapshot);
        if (subLen != expectedSize)
            return;

        // Phase 6: client's smoothed RTT estimate, used to size the
        // lag-compensation rewind window for this client's hitscans.
        uint16_t rttMs = 0;
        std::memcpy(&rttMs, sub + 1, sizeof(uint16_t));
        conn.lastReportedRttMs = rttMs;

        const uint8_t* base = sub + 3;
        for (uint8_t i = 0; i < count; ++i) {
            InputSnapshot snap{};
            std::memcpy(&snap, base + i * sizeof(InputSnapshot), sizeof(InputSnapshot));
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
        // Phase 3d-3: PING/PONG over UDP. Echo the timestamp payload
        // back; the client's RTT measurement now isn't poisoned by
        // any TCP-stream backlog. Reply via UDP straight back to the
        // source we just learned.
        if (subLen != sizeof(Uint64))
            return;
        uint8_t reply[1 + sizeof(Uint64)];
        reply[0] = static_cast<uint8_t>(PacketType::PONG);
        std::memcpy(reply + 1, sub, sizeof(Uint64));

        net::PacketHeader hdr{};
        hdr.kind = static_cast<uint8_t>(net::PacketKind::Payload);
        hdr.connectionId = conn.connectionId;
        hdr.sequence = 0;
        hdr.channel = static_cast<uint8_t>(net::ChannelId::Unreliable);
        udpEndpoint_.send(conn.udpAddr, hdr, reply, static_cast<int>(sizeof(reply)));
        break;
    }
    default:
        // Other types still ride TCP. Silently drop unknown UDP types
        // — could be stale packets from a previous session, or a
        // client speaking a future protocol version.
        break;
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

        // ── Phase 3d: UDP receive phase ──────────────────────────────
        //
        // Drain all available datagrams in one batch. Held under the
        // state mutex so handleUdpInput's enqueue path is serialized
        // with the game thread's dequeueEvent.
        if (udpEndpoint_.isOpen()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            net::UdpReceivedMessage msg;
            int drained = 0;
            constexpr int k_maxDatagramsPerCycle = 512;
            while (drained < k_maxDatagramsPerCycle && udpEndpoint_.tryReceive(msg)) {
                ++drained;
                // Stage 3d-2/3: INPUT and PING/PONG ride this channel.
                // Channel + connectionId are validated; payload is the
                // standard `[PacketType][rest]` wire format mirrored
                // from TCP.
                if (msg.header.channel == static_cast<uint8_t>(net::ChannelId::Unreliable) &&
                    msg.header.connectionId != 0)
                {
                    handleUdpUnreliable(msg.header.connectionId,
                                        msg.from,
                                        msg.payload.data(),
                                        static_cast<uint32_t>(msg.payload.size()));
                }
                msg.from.release();
            }
        }

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            flushAllOutbound();
        }

        // ── Phase 3d-5: drain reliable-event queues over UDP ────────
        //
        // For each client with a known UDP address, send every pending
        // reliable event once this cycle and decrement its remaining-
        // send budget. Pop entries whose budget hit zero. Each event
        // gets shipped k_reliableRedundancy times across consecutive
        // cycles so a single dropped datagram doesn't lose the event
        // (and the client's sequence-based dedup catches the dups).
        if (udpEndpoint_.isOpen()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            for (auto& [_, conn] : clients) {
                if (conn.udpAddr.addr == nullptr || conn.reliableQueue.empty())
                    continue;
                for (auto& entry : conn.reliableQueue) {
                    net::PacketHeader hdr{};
                    hdr.kind = static_cast<uint8_t>(net::PacketKind::Payload);
                    hdr.connectionId = conn.connectionId;
                    hdr.sequence = entry.sequence;
                    hdr.channel = static_cast<uint8_t>(net::ChannelId::ReliableOrdered);
                    udpEndpoint_.send(conn.udpAddr, hdr, entry.framed.data(), static_cast<int>(entry.framed.size()));
                    if (entry.remainingSends > 0)
                        --entry.remainingSends;
                }
                while (!conn.reliableQueue.empty() && conn.reliableQueue.front().remainingSends == 0)
                    conn.reliableQueue.pop_front();
            }
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

        // Phase 3d-1: mint a 32-bit random connection ID. Off-path
        // attackers can't guess it (defends against UDP spoofing for the
        // duration of this connection). The ID is shipped to the client
        // in the ASSIGN_CLIENT_ID packet over TCP and stamped on every
        // outbound UDP datagram from then on.
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<uint32_t> dist{1, std::numeric_limits<uint32_t>::max()};
        const uint32_t connId = dist(rng);

        clients.insert({clientId,
                        Connection{.msgStream = MessageStream(socket),
                                   .clientId = clientId,
                                   .pendingInitialization = true,
                                   .connectionId = connId}});
        connIdToClient_[connId] = clientId;
        eventQueue.enqueue(Event{.clientId = clientId, .type = EventType::Connected, .movementIntent = {}});
    }
}

void Server::disconnectClient(Connection conn)
{
    SDL_Log("Server: disconnecting client %d", conn.clientId.value);
    NET_DestroyStreamSocket(conn.msgStream.socket);
    if (conn.connectionId != 0)
        connIdToClient_.erase(conn.connectionId);
    conn.udpAddr.release();
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
        // Multi-input wire format:
        //   [count u8] [rttMs u16] [InputSnapshot * count],
        // oldest-first. Each client packet carries the last
        // Client::k_inputRedundancy inputs. We dedup against
        // conn.lastAppliedInputTick — most entries in any given packet are
        // duplicates of already-applied snapshots and get skipped cheaply.
        // The rttMs prefix is the client's smoothed RTT estimate; the
        // server uses RTT/2 as the rewind window for lag-compensated
        // hitscan (Phase 6).
        if (payloadLen < 3) {
            SDL_Log("Server: received undersized INPUT packet from client %d", conn.clientId.value);
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

        const uint32_t expectedSize = 3u + static_cast<uint32_t>(count) * sizeof(InputSnapshot);
        if (payloadLen != expectedSize) {
            SDL_Log("Server: received INPUT packet of invalid size %u (expected %u for count %u)",
                    payloadLen,
                    expectedSize,
                    static_cast<unsigned>(count));
            return;
        }

        // Phase 6: client's smoothed RTT, persisted on the connection
        // so the lag-comp scheduler can read it each tick when it sets
        // the per-shooter rewind target.
        uint16_t rttMs = 0;
        std::memcpy(&rttMs, payload + 1, sizeof(uint16_t));
        conn.lastReportedRttMs = rttMs;

        const uint8_t* base = payload + 3;
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
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = clients.find(clientId);
    if (it == clients.end())
        return false;

    // Phase 3d-1: ASSIGN_CLIENT_ID payload now also carries the
    // server-minted UDP connectionId so the client can stamp it on
    // outbound UDP datagrams. Wire format:
    //   [PacketType::ASSIGN_CLIENT_ID (1B)] [entt::entity] [uint32_t connectionId]
    uint8_t buf[1 + sizeof(entt::entity) + sizeof(uint32_t)];
    buf[0] = static_cast<uint8_t>(PacketType::ASSIGN_CLIENT_ID);
    std::memcpy(buf + 1, &playerEntity, sizeof(entt::entity));
    std::memcpy(buf + 1 + sizeof(entt::entity), &it->second.connectionId, sizeof(uint32_t));

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

    std::lock_guard<std::mutex> lock(stateMutex_);

    // Phase 3d-4: when the snapshots-over-UDP toggle is on, fragment
    // the snapshot and ship it via UDP to every client whose udpAddr
    // we know (i.e. that has sent us at least one UDP packet — see
    // handleUdpUnreliable's address caching). Clients we haven't
    // heard from over UDP yet fall back to the TCP path; they catch
    // up to UDP within ~one snapshot interval after their first
    // INPUT/PING goes through.
    const bool useUdp = transportConfig_.snapshotsOverUdp && udpEndpoint_.isOpen();

    for (auto& [_, conn] : clients) {
        const bool canUseUdp = useUdp && conn.udpAddr.addr != nullptr;
        if (canUseUdp) {
            net::PacketHeader hdr{};
            hdr.kind = static_cast<uint8_t>(net::PacketKind::Payload);
            hdr.connectionId = conn.connectionId;
            hdr.sequence = conn.udpSnapshotSequence++;
            hdr.channel = static_cast<uint8_t>(net::ChannelId::Unreliable);
            // sendFragmented handles both the single-datagram case
            // (small registries, e.g. early in a match) and the multi-
            // fragment case (~5 KB at 100 players → ~5 fragments). It
            // sets flags.fragmented + fragmentInfo per fragment.
            udpEndpoint_.sendFragmented(conn.udpAddr, hdr, buf.data(), static_cast<int>(buf.size()));
        } else {
            // TCP fallback path: the per-client OutboundQueue with
            // replace-on-stale (Phase 3a) — same as before 3d-4.
            conn.outbound.enqueue(static_cast<uint8_t>(PacketType::UPDATE_REGISTRY),
                                  frameMessage(buf.data(), static_cast<int>(buf.size())));
        }
    }
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

    enqueueReliableEvent(buf.data(), static_cast<int>(buf.size()));
}

int Server::getClientCount()
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return static_cast<int>(clients.size());
}

uint16_t Server::getClientRttMs(ClientId clientId)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    const auto it = clients.find(clientId);
    if (it == clients.end())
        return 0;
    return it->second.lastReportedRttMs;
}

void Server::broadcastMatchStatus(MatchStatePacket packet)
{
    std::vector<uint8_t> buf(sizeof(PacketType) + sizeof(MatchStatePacket));
    buf[0] = static_cast<uint8_t>(PacketType::MATCH_STATE);
    std::memcpy(buf.data() + 1, &packet, sizeof(MatchStatePacket));

    enqueueReliableEvent(buf.data(), static_cast<int>(buf.size()));
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

    enqueueReliableEvent(buf.data(), static_cast<int>(buf.size()));

    SDL_Log("Server: broadcasted %u kill events to clients", count);
}
