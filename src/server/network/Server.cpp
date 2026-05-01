/// @file Server.cpp
/// @brief Implementation of the TCP game server.

#include "Server.hpp"

#include "ecs/components/ClientId.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "network/MatchStatus.hpp"
#include "network/PacketType.hpp"
#include "network/RegistrySerialization.hpp"
#include "network/transport/PacketHeader.hpp"
#include "perf/Parallel.hpp" // PR-9: parallelFor for per-client syscall fan-out.
#include "perf/Profiler.hpp" // PR-1: NetworkCounters & scope timers.
#include "systems/EventQueue.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <cstring>
#include <entt/entity/entity.hpp>
#include <memory>
#include <random>
#include <utility>

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

    // PR-2c: EventQueue holds a mutex now; reset by draining instead of
    // assignment (mutex is non-copyable / non-assignable). At init
    // time the queue should already be empty, but draining is cheap.
    {
        std::vector<Event> drained;
        eventQueue.drainAll(drained);
    }
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

    std::unique_lock<std::shared_mutex> lock(stateMutex_);
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
    // PR-6: shared lock — we only read the clients map structure
    // (lookup) and write into the target's `outbound` queue, which
    // self-locks (PR-5b).
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    auto it = clients.find(clientId);
    if (it == clients.end())
        return false;

    it->second.outbound.enqueue(replaceKey, frameMessage(data, len));
    return true;
}

void Server::enqueueBroadcast(uint8_t replaceKey, const void* data, int len)
{
    // PR-4 (server-perf): build the framed bytes ONCE into a
    // shared_ptr; per-client enqueue is a pointer copy. Pre-PR-4 this
    // copied the framed `std::vector<uint8_t>` per client — fine at
    // ~10 clients but quadratic-feeling at 200 clients × 128 Hz of
    // matchController-driven broadcasts (the per-tick MATCH_STATE
    // broadcast was the dominant contributor to the `match` scope's
    // 100+ ms p99 spike at 200 bots).
    auto framed = std::make_shared<const std::vector<uint8_t>>(frameMessage(data, len));

    // PR-6: shared lock — we read the clients map and write into each
    // client's `outbound`, which self-locks (PR-5b). Multiple
    // game-thread broadcasts can run concurrently with each other and
    // with the network thread's `flushAllOutbound` phase 2.
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    for (auto& [_, conn] : clients) {
        conn.outbound.enqueue(replaceKey, framed);
    }
}

void Server::flushAllOutbound()
{
    // Per-tick max-age for unreliable entries. Anything older than this is
    // dropped before going on the wire — see OutboundQueue::flushTo.
    constexpr Uint32 k_maxAgeMs = 300;

    // PR-5b (server-perf): three-phase lock-light flush.
    //
    //  Phase 1 (under stateMutex_, brief): snapshot per-client
    //          (ClientId, OutboundQueue*, socket) into a flat list.
    //          Pointer stability holds because the network thread is
    //          the sole writer to `clients` and we're on it; no
    //          concurrent insert/erase can happen between phase 1
    //          and phase 3.
    //  Phase 2 (lock-free): iterate the list, calling
    //          OutboundQueue::flushTo per client. The queue is
    //          self-locked (PR-5b) so per-client enqueues from the
    //          game thread proceed in parallel. The slow part — the
    //          NET_WriteToStreamSocket syscall — runs without
    //          stateMutex_, so game-thread broadcasts don't wait.
    //  Phase 3 (under stateMutex_, brief): apply any disconnects
    //          phase 2 collected, then republish the atomic
    //          getClientCount / snapshotClientRtts snapshots.
    //
    // Pre-PR-5b this whole loop ran under a single stateMutex_ hold;
    // at 300 clients the syscall fan-out alone took 25-50 ms p99 on
    // the loadtest harness, blocking every game-thread broadcast for
    // that duration.

    struct Target
    {
        ClientId id;
        OutboundQueue* queue;
        NET_StreamSocket* socket;
    };
    static thread_local std::vector<Target> targets;
    targets.clear();

    {
        // PR-6: shared lock — we only read the clients map.
        // Pointers stay valid because the only writers are this
        // thread (acceptClients / disconnect-application) and we're
        // not running those concurrently with this snapshot.
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        targets.reserve(clients.size());
        for (auto& [id, conn] : clients) {
            targets.push_back(Target{id, &conn.outbound, conn.msgStream.socket});
        }
    }

    // PR-9 (server-perf): parallelize the per-client flush. SDL_net's
    // NET_WriteToStreamSocket is documented thread-safe per-socket,
    // and we hold one socket per Target (each Target has its own
    // OutboundQueue + its own NET_StreamSocket*), so multiple TBB
    // workers each draining a different client's queue is safe.
    //
    // The shared writes here are:
    //   - `failed`: per-thread-local accumulator merged at the end
    //   - `maxDepth`: atomic max via cmpxchg loop
    //   - perf counters inside flushTo: already std::atomic
    std::atomic<std::uint32_t> maxDepthAtomic{0};
    std::mutex failedMutex;
    std::vector<ClientId> failed;

    auto flushKernel = [&](const Target& t) {
        const auto depth = static_cast<std::uint32_t>(t.queue->depth());
        std::uint32_t cur = maxDepthAtomic.load(std::memory_order_relaxed);
        while (depth > cur && !maxDepthAtomic.compare_exchange_weak(cur, depth, std::memory_order_relaxed)) {
        }
        if (!t.queue->flushTo(t.socket, k_maxAgeMs)) {
            // Disconnects are rare; a brief mutex on the failure
            // list is cheaper than a per-thread vector + merge.
            std::lock_guard<std::mutex> fl(failedMutex);
            failed.push_back(t.id);
        }
    };

    ::group2::perf::parallelFor(targets.begin(), targets.end(), flushKernel);
    const std::uint32_t maxDepth = maxDepthAtomic.load(std::memory_order_relaxed);

    // Phase 3: apply failures + republish atomics. Take the lock once.
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        for (ClientId id : failed) {
            if (auto it = clients.find(id); it != clients.end()) {
                disconnectClient(it->second);
                clients.erase(it);
            }
        }

        auto& nc = ::group2::perf::net();
        nc.clientCount.store(static_cast<std::uint32_t>(clients.size()), std::memory_order_relaxed);
        std::uint32_t cur = nc.peakBacklog.load(std::memory_order_relaxed);
        while (maxDepth > cur && !nc.peakBacklog.compare_exchange_weak(cur, maxDepth, std::memory_order_relaxed)) {
        }

        // PR-4: publish lock-free read snapshots.
        clientCountAtomic_.store(static_cast<std::uint32_t>(clients.size()), std::memory_order_relaxed);
        auto rttSnap = std::make_shared<ClientRttSnapshot>();
        rttSnap->entries.reserve(clients.size());
        for (const auto& [id, conn] : clients) {
            rttSnap->entries.push_back(ClientNetState{
                .id = id,
                .rttMs = conn.lastReportedRttMs,
                .interpDelaySnapshots = conn.lastReportedInterpDelaySnapshots,
            });
        }
        rttSnapshotAtomic_.store(std::shared_ptr<const ClientRttSnapshot>(std::move(rttSnap)),
                                 std::memory_order_release);
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

    // PR-5a (server-perf): build the bytes ONCE into a shared_ptr.
    // Every per-client enqueue (UDP reliable queue OR TCP fallback)
    // is a pointer copy, not a vector copy. Pre-PR-5a:
    //   - UDP path: `framed = framed` copied a vector per client
    //   - TCP path: `frameMessage(data, len)` allocated a fresh
    //     framed vector per client AND OutboundQueue::enqueue copied
    //     it again into the deque entry
    // Both contributed to the `broadcastEvents` p99 = 25-50 ms spike
    // at 300 bots during fire-burst ticks.
    {
        // Raw payload (no length prefix) for the UDP reliable path.
        auto rawPayload = std::vector<uint8_t>(static_cast<size_t>(len));
        std::memcpy(rawPayload.data(), data, static_cast<size_t>(len));
        auto sharedRaw = std::make_shared<const std::vector<uint8_t>>(std::move(rawPayload));

        // 4-byte length-prefixed framed payload for the TCP fallback.
        auto sharedFramed = std::make_shared<const std::vector<uint8_t>>(frameMessage(data, len));

        std::unique_lock<std::shared_mutex> lock(stateMutex_);

        if (!transportConfig_.eventsOverUdp || !udpEndpoint_.isOpen()) {
            // TCP fallback. replaceKey = 0 → never drop on age, always ship.
            for (auto& [_, conn] : clients) {
                conn.outbound.enqueue(0, sharedFramed);
            }
            return;
        }

        for (auto& [_, conn] : clients) {
            conn.reliableQueue.push_back(Connection::PendingReliableEvent{
                .sequence = conn.reliableNextSequence++,
                .remainingSends = k_reliableRedundancy,
                .framed = sharedRaw,
            });
        }
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
        // Same parser as the TCP INPUT case. Wire format (PR-12):
        //   [count u8] [rttMs u16] [interpDelaySnapshots u8] [InputSnapshot * count]
        if (subLen < 4)
            return;
        const uint8_t count = sub[0];
        constexpr uint8_t k_maxInputsPerPacket = 16;
        if (count == 0 || count > k_maxInputsPerPacket)
            return;
        const uint32_t expectedSize = 4u + static_cast<uint32_t>(count) * sizeof(InputSnapshot);
        if (subLen != expectedSize)
            return;

        // Phase 6: client's smoothed RTT estimate, used to size the
        // lag-compensation rewind window for this client's hitscans.
        uint16_t rttMs = 0;
        std::memcpy(&rttMs, sub + 1, sizeof(uint16_t));
        conn.lastReportedRttMs = rttMs;

        // PR-12: client's render-delay (in snapshots) — see
        // Connection::lastReportedInterpDelaySnapshots for the lag-comp
        // formula that consumes this.  Clamped to the
        // InterpolationBuffer capacity to defend against malformed
        // values; well-behaved clients never exceed 8.
        const uint8_t interpDelaySnapshots = std::min<uint8_t>(sub[3], 8);
        conn.lastReportedInterpDelaySnapshots = interpDelaySnapshots;

        const uint8_t* base = sub + 4;
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
            std::unique_lock<std::shared_mutex> lock(stateMutex_);
            acceptClients();
        }
        // PR-6: readClients now manages its own shared/unique split.
        readClients();

        // ── Phase 3d: UDP receive phase ──────────────────────────────
        //
        // PR-6: shared lock. handleUdpUnreliable reads the clients map
        // (lookup by connectionId), updates per-Connection state
        // (lastReportedRttMs, udpAddr) — those are written only by
        // the network thread (this code), so no race with concurrent
        // shared lockers reading per-Conn state. eventQueue.enqueue
        // is self-locked. PONG send (`udpEndpoint_.send`) doesn't
        // touch shared state.
        if (udpEndpoint_.isOpen()) {
            std::shared_lock<std::shared_mutex> lock(stateMutex_);
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

        // ── PR-2: deferred snapshot fanout (lock-light) ─────────────
        //
        // `broadcastRegistry` publishes shared_ptr buffers; this phase
        // fans them out to clients. The fanout has two parts with
        // different locking needs:
        //   1. Snapshot the per-client UDP-destination + TCP-queue
        //      addresses while the lock is held. O(N), microseconds.
        //   2. Issue sendto() per UDP target, OR enqueue() into each
        //      TCP queue. The UDP sends are syscalls that DO NOT
        //      need stateMutex_; the TCP enqueue mutates per-client
        //      OutboundQueue which currently lives behind the mutex.
        //
        // We split the lock so the game thread isn't blocked on
        // hundreds of sendto() syscalls — exactly the symptom that
        // tanked tick p99 to 50 ms at 100 bots in PR-2's first cut.
        {
            std::shared_ptr<const std::vector<uint8_t>> payload;
            std::shared_ptr<const std::vector<uint8_t>> framed;
            {
                std::unique_lock<std::shared_mutex> lock(stateMutex_);
                payload = std::exchange(pendingSnapshotPayload_, nullptr);
                framed = std::exchange(pendingSnapshotFramed_, nullptr);
            }

            if (payload || framed) {
                // Step 1: collect per-client send targets under lock.
                // We only read the small fields — udpAddr, connectionId,
                // sequence — and bump the per-client sequence counter
                // here so the same client never gets duplicate
                // sequences for two snapshots even if a fanout takes
                // longer than a cycle.
                struct UdpTarget
                {
                    net::UdpEndpointAddr addr; // ref-counted; we'll release
                    uint32_t connectionId;
                    uint16_t sequence;
                };
                static thread_local std::vector<UdpTarget> udpTargets;
                static thread_local std::vector<ClientId> tcpTargets;
                udpTargets.clear();
                tcpTargets.clear();

                {
                    // PR-6: shared lock — we read the clients map and
                    // bump per-Conn `udpSnapshotSequence`, which is
                    // single-writer (this thread).
                    std::shared_lock<std::shared_mutex> lock(stateMutex_);
                    const bool useUdp = transportConfig_.snapshotsOverUdp && udpEndpoint_.isOpen();
                    for (auto& [id, conn] : clients) {
                        const bool canUseUdp = useUdp && conn.udpAddr.addr != nullptr;
                        if (canUseUdp && payload) {
                            UdpTarget t;
                            t.addr.addr = NET_RefAddress(conn.udpAddr.addr);
                            t.addr.port = conn.udpAddr.port;
                            t.connectionId = conn.connectionId;
                            t.sequence = conn.udpSnapshotSequence++;
                            udpTargets.push_back(t);
                        } else if (framed) {
                            // Defer TCP enqueue — that path mutates
                            // per-client state that is mutex-protected.
                            // We collect IDs here and re-acquire the
                            // lock in step 3.
                            tcpTargets.push_back(id);
                        }
                    }
                }

                // Step 2: UDP sends. Lock-free — sendto only touches the
                // socket FD and the dest address. With 100 bots × ~5
                // fragments per snapshot × 32 Hz, this is ~16k syscalls/s
                // off the game thread's critical path.
                //
                // PR-9 (server-perf): parallelize the fan-out across
                // TBB workers. NET_SendDatagram is documented thread-
                // safe per-socket; multiple workers calling
                // sendFragmented on the same UDP socket with different
                // dest addresses race only on the socket FD's send
                // buffer (kernel-side), which is the protocol stack's
                // job to serialize. At 500 clients × 5 fragments × 32 Hz
                // = 80k syscalls/sec — clearly worth fanning out.
                if (payload && !udpTargets.empty()) {
                    auto udpKernel = [this, &payload](UdpTarget& t) {
                        net::PacketHeader hdr{};
                        hdr.kind = static_cast<uint8_t>(net::PacketKind::Payload);
                        hdr.connectionId = t.connectionId;
                        hdr.sequence = t.sequence;
                        hdr.channel = static_cast<uint8_t>(net::ChannelId::Unreliable);
                        udpEndpoint_.sendFragmented(t.addr, hdr, payload->data(), static_cast<int>(payload->size()));
                        t.addr.release();
                    };
                    ::group2::perf::parallelFor(udpTargets.begin(), udpTargets.end(), udpKernel);
                }

                // Step 3: TCP enqueue. Brief lock; per-client work is a
                // shared_ptr copy (PR-2) so the inner loop is O(N)
                // pointer copies, not O(N) memcpys.
                if (framed && !tcpTargets.empty()) {
                    // PR-6: shared lock — per-Conn outbound is self-locked.
                    std::shared_lock<std::shared_mutex> lock(stateMutex_);
                    for (const ClientId& id : tcpTargets) {
                        if (auto it = clients.find(id); it != clients.end()) {
                            it->second.outbound.enqueue(static_cast<uint8_t>(PacketType::UPDATE_REGISTRY), framed);
                        }
                    }
                }
            }
        }

        // PR-5b: flushAllOutbound now manages its own locking (brief
        // snapshot + apply phases) so it can run the per-client
        // syscall fan-out without `stateMutex_` held. The caller
        // does NOT lock around it any more.
        flushAllOutbound();

        // ── Phase 3d-5: drain reliable-event queues over UDP ────────
        //
        // For each client with a known UDP address, send every pending
        // reliable event once this cycle and decrement its remaining-
        // send budget. Pop entries whose budget hit zero. Each event
        // gets shipped k_reliableRedundancy times across consecutive
        // cycles so a single dropped datagram doesn't lose the event
        // (and the client's sequence-based dedup catches the dups).
        if (udpEndpoint_.isOpen()) {
            std::unique_lock<std::shared_mutex> lock(stateMutex_);
            for (auto& [_, conn] : clients) {
                if (conn.udpAddr.addr == nullptr || conn.reliableQueue.empty())
                    continue;
                for (auto& entry : conn.reliableQueue) {
                    net::PacketHeader hdr{};
                    hdr.kind = static_cast<uint8_t>(net::PacketKind::Payload);
                    hdr.connectionId = conn.connectionId;
                    hdr.sequence = entry.sequence;
                    hdr.channel = static_cast<uint8_t>(net::ChannelId::ReliableOrdered);
                    if (entry.framed) {
                        udpEndpoint_.send(
                            conn.udpAddr, hdr, entry.framed->data(), static_cast<int>(entry.framed->size()));
                    }
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

        // PR-5b (server-perf): construct in place. `Connection` is no
        // longer copyable (its `OutboundQueue` member now holds a
        // `std::mutex`), so the previous `insert({key, Connection{...}})`
        // form would synthesize a copy. `try_emplace` default-constructs
        // the value and we fill in the fields after.
        auto [it, inserted] = clients.try_emplace(clientId);
        if (inserted) {
            auto& conn = it->second;
            conn.msgStream = MessageStream(socket);
            conn.clientId = clientId;
            conn.pendingInitialization = true;
            conn.connectionId = connId;
        }
        connIdToClient_[connId] = clientId;
        eventQueue.enqueue(Event{.clientId = clientId, .type = EventType::Connected, .movementIntent = {}});
    }
}

void Server::disconnectClient(Connection& conn)
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
    //
    // PR-1: count inbound bytes + packets at the dispatcher boundary.
    // We observe `size` directly inside the callback — it's the
    // length of the wire message without its 4-byte length prefix,
    // which is the closest thing we have to "useful payload bytes
    // received." Adding the 4-byte framing accounts for total wire
    // bytes; we do that with a single per-message correction.
    //
    // PR-6 (server-perf): two-phase read with shared / unique split.
    //
    //   Phase 1 (shared lock): iterate clients, drive each socket's
    //           msgStream.poll. Per-Connection state (`recvBuf`,
    //           `lastAppliedInputTick`, `lastReportedRttMs`,
    //           `udpAddr`) is single-writer (this thread), so other
    //           shared-lock holders (game-thread `enqueueBroadcast`,
    //           `notifyPlayerClientId`) don't race. Failed sockets
    //           are collected for deferred disconnect.
    //
    //   Phase 2 (unique lock, only if any failed): apply disconnects.
    //           Briefer than the read pass; runs after we've already
    //           given every healthy client its read cycle.
    //
    // Pre-PR-6 this whole loop ran under a unique lock, blocking
    // every game-thread broadcast / lookup for the full pass — at
    // 300 clients that pass took 25-50 ms p99 wall time and starved
    // the simulation thread.
    auto& nc = ::group2::perf::net();

    static thread_local std::vector<ClientId> failed;
    failed.clear();

    {
        std::shared_lock<std::shared_mutex> lock(stateMutex_);
        for (auto& [_, conn] : clients) {
            bool ok = conn.msgStream.poll([this, &conn, &nc](const void* data, Uint32 size) {
                nc.bytesRecv.fetch_add(size + sizeof(Uint32), std::memory_order_relaxed);
                nc.packetsRecv.fetch_add(1, std::memory_order_relaxed);
                handleMessage(conn, data, size);
            });
            if (!ok) {
                failed.push_back(conn.clientId);
            }
        }
    }

    if (!failed.empty()) {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        for (ClientId id : failed) {
            if (auto it = clients.find(id); it != clients.end()) {
                disconnectClient(it->second);
                clients.erase(it);
            }
        }
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
        // Multi-input wire format (PR-12):
        //   [count u8] [rttMs u16] [interpDelaySnapshots u8] [InputSnapshot * count]
        // oldest-first. Each client packet carries the last
        // Client::k_inputRedundancy inputs. We dedup against
        // conn.lastAppliedInputTick — most entries in any given packet are
        // duplicates of already-applied snapshots and get skipped cheaply.
        // The rttMs prefix is the client's smoothed RTT estimate; the
        // interpDelaySnapshots byte is the client's render-delay (PR-11).
        // Both feed the lag-comp formula in updateLagCompTargets.
        if (payloadLen < 4) {
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

        const uint32_t expectedSize = 4u + static_cast<uint32_t>(count) * sizeof(InputSnapshot);
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

        // PR-12: client's render-delay (in snapshots) — feeds the
        // lag-comp formula alongside RTT/2 so the server rewinds to
        // exactly the world state the client SAW when firing, not
        // merely to RTT/2 ago.  Clamped to InterpolationBuffer capacity
        // to defend against malformed packets.
        const uint8_t interpDelaySnapshots = std::min<uint8_t>(payload[3], 8);
        conn.lastReportedInterpDelaySnapshots = interpDelaySnapshots;

        const uint8_t* base = payload + 4;
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
    // PR-2c: EventQueue self-locks. No outer stateMutex_ needed —
    // draining events no longer competes with the network thread's
    // long-running readClients/UDP-recv phases for the same lock.
    return eventQueue.isEmpty();
}

Event Server::dequeueEvent()
{
    return eventQueue.dequeue();
}

void Server::drainEvents(std::vector<Event>& out)
{
    eventQueue.drainAll(out);
}

// NOTE: playerEntity is the entity id of the player
bool Server::notifyPlayerClientId(ClientId clientId, entt::entity playerEntity)
{
    // PR-6: shared lock — we look up by clientId, set
    // `pendingInitialization=false` (per-Conn write, single writer:
    // this is only called once per client init from the game thread),
    // and call per-Conn `outbound.enqueue` (self-locked).
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
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
    // Two buffers because the two transports want different shapes:
    //   - UDP: raw payload `[PacketType][...wire fields]`. The
    //     PacketHeader is added per-fragment by `sendFragmented`.
    //   - TCP: framed `[len:u32][PacketType][...wire fields]`. Goes
    //     straight into OutboundQueue → NET_WriteToStreamSocket.
    //
    // Both are immutable post-publish. The shared_ptr lets N clients
    // observe the same bytes via N pointer copies.
    //
    // PR-10 (server-perf): wire format gains a snapshot tick + an
    // optional delta variant.
    //
    //   Full snapshot (UPDATE_REGISTRY):
    //     [PacketType:u8] [tick:u32] [serializedBytes...]
    //
    //   Delta snapshot (UPDATE_REGISTRY_DELTA):
    //     [PacketType:u8] [tick:u32] [fromTick:u32]
    //     [baselineSize:u32] [rlePatch...]
    //
    // We always serialize the full registry first, then decide which
    // form to ship by patch size. Force a full keyframe every Nth
    // snapshot so clients that lost a delta resync within ≤ 500 ms.

    constexpr std::uint32_t k_keyframeInterval = 16;

    auto raw = registry_serialization::serialize(registry);
    const std::uint32_t thisTick = ++snapshotCounter_;
    const bool forceFull =
        prevSnapshotRaw_.empty() || prevSnapshotTick_ == 0 || (snapshotCounter_ % k_keyframeInterval) == 0;

    std::vector<uint8_t> wirePayload; // [PacketType][...] — UDP raw form
    bool sentDelta = false;

    if (!forceFull && raw.size() == prevSnapshotRaw_.size()) {
        auto patch = registry_serialization::encodeDelta(prevSnapshotRaw_, raw);
        // Only ship a delta if the patch beats the full by >25 %.
        // Otherwise the per-client wire saving doesn't justify the
        // extra encode/decode CPU. Threshold is pragmatic; revisit
        // once we have a real-traffic profile.
        const std::size_t deltaWireSize = 1 /*PacketType*/ + sizeof(std::uint32_t) /*tick*/ +
                                          sizeof(std::uint32_t) /*fromTick*/ + sizeof(std::uint32_t) /*size*/ +
                                          patch.size();
        const std::size_t fullWireSize = 1 + sizeof(std::uint32_t) + raw.size();
        if (deltaWireSize * 4 < fullWireSize * 3) {
            // Build delta wire payload.
            wirePayload.reserve(deltaWireSize);
            wirePayload.push_back(static_cast<uint8_t>(PacketType::UPDATE_REGISTRY_DELTA));
            const auto t = thisTick;
            const auto ft = prevSnapshotTick_;
            const auto sz = static_cast<std::uint32_t>(raw.size());
            const auto* tp = reinterpret_cast<const uint8_t*>(&t);
            const auto* fp = reinterpret_cast<const uint8_t*>(&ft);
            const auto* sp = reinterpret_cast<const uint8_t*>(&sz);
            wirePayload.insert(wirePayload.end(), tp, tp + sizeof(t));
            wirePayload.insert(wirePayload.end(), fp, fp + sizeof(ft));
            wirePayload.insert(wirePayload.end(), sp, sp + sizeof(sz));
            wirePayload.insert(wirePayload.end(), patch.begin(), patch.end());
            sentDelta = true;
        }
    }

    if (!sentDelta) {
        // Full snapshot path.
        wirePayload.reserve(1 + sizeof(std::uint32_t) + raw.size());
        wirePayload.push_back(static_cast<uint8_t>(PacketType::UPDATE_REGISTRY));
        const auto t = thisTick;
        const auto* tp = reinterpret_cast<const uint8_t*>(&t);
        wirePayload.insert(wirePayload.end(), tp, tp + sizeof(t));
        wirePayload.insert(wirePayload.end(), raw.begin(), raw.end());
    }

    // Save the just-serialized RAW (no packet prefix) bytes as the
    // baseline for the *next* delta. We do this AFTER deciding the
    // wire form so the delta we sent referenced the OLD prev.
    prevSnapshotRaw_ = std::move(raw);
    prevSnapshotTick_ = thisTick;

    const std::size_t wireBytes = wirePayload.size();

    // Build the TCP-framed buffer once.
    std::vector<uint8_t> framedBuf(sizeof(Uint32) + wireBytes);
    {
        const auto msgLen = static_cast<Uint32>(wireBytes);
        std::memcpy(framedBuf.data(), &msgLen, sizeof(msgLen));
        std::memcpy(framedBuf.data() + sizeof(msgLen), wirePayload.data(), wireBytes);
    }

    auto payload = std::make_shared<const std::vector<uint8_t>>(std::move(wirePayload));
    auto framed = std::make_shared<const std::vector<uint8_t>>(std::move(framedBuf));

    std::size_t fanout = 0;
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex_);
        // Replace any in-flight snapshot the network thread hasn't
        // picked up yet — only the freshest is meaningful.
        pendingSnapshotPayload_ = std::move(payload);
        pendingSnapshotFramed_ = std::move(framed);
        fanout = clients.size();
    }

    auto& nc = ::group2::perf::net();
    nc.bytesSent.fetch_add(static_cast<std::uint64_t>(wireBytes) * fanout, std::memory_order_relaxed);
    nc.snapshotsSent.fetch_add(fanout, std::memory_order_relaxed);
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
    // PR-4 (server-perf): atomic gauge — published from
    // flushAllOutbound on the network thread. Pre-PR-4 this acquired
    // stateMutex_ and competed with the network thread's
    // long-running readClients pass; at 200+ bots that put it on the
    // matchController hot path's p99 spike list. Now lock-free.
    return static_cast<int>(clientCountAtomic_.load(std::memory_order_relaxed));
}

uint16_t Server::getClientRttMs(ClientId clientId)
{
    // PR-6: shared lock — pure read. Modern callers should use
    // snapshotClientRtts (lock-free) instead; this single-client
    // form is kept for compat.
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    const auto it = clients.find(clientId);
    if (it == clients.end())
        return 0;
    return it->second.lastReportedRttMs;
}

void Server::snapshotClientNetStates(std::vector<ClientNetState>& out)
{
    // PR-4 (server-perf): read the atomic-published RTT cache the
    // network thread maintains.  PR-12 extended each entry from
    // `(id, rttMs)` to `(id, rttMs, interpDelaySnapshots)` so the
    // lag-comp scheduler reads both rewind terms in one fetch.
    // Lock-free, at most one network-cycle (~1 ms) stale — well below
    // the lag-comp scheduler's half-RTT-rounded-to-ticks resolution.
    //
    // Falls back to a freshly-built mutex-protected snapshot only on
    // the very first call before the network thread has published
    // anything. After that, every call is lock-free.
    out.clear();
    auto cached = rttSnapshotAtomic_.load(std::memory_order_acquire);
    if (cached) {
        out.reserve(cached->entries.size());
        out.assign(cached->entries.begin(), cached->entries.end());
        return;
    }

    // Cold path: no published snapshot yet. Take a shared lock so we
    // don't race with concurrent inserts by acceptClients (which
    // takes unique). Pure read.
    std::shared_lock<std::shared_mutex> lock(stateMutex_);
    out.reserve(clients.size());
    for (const auto& [id, conn] : clients) {
        out.push_back(ClientNetState{
            .id = id,
            .rttMs = conn.lastReportedRttMs,
            .interpDelaySnapshots = conn.lastReportedInterpDelaySnapshots,
        });
    }
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
