/// @file Client.cpp
/// @brief Implementation of the TCP client connection and message I/O.

#include "Client.hpp"

#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "network/MatchStatus.hpp"
#include "network/NetKillEvent.hpp"
#include "network/PacketType.hpp"
#include "network/RegistrySerialization.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <cstring>

bool Client::init(const char* addr, Uint16 port)
{
    serverAddr = NET_ResolveHostname(addr);
    if (NET_WaitUntilResolved(serverAddr, -1) == NET_FAILURE) {
        SDL_Log("Failed to resolve server address: %s", SDL_GetError());
        return false;
    }

    auto sock = NET_CreateClient(serverAddr, port);
    if (!sock) {
        SDL_Log("Failed to create client %s", SDL_GetError());
        return false;
    }
    NET_SetStreamSocketNoDelay(sock, true);

    if (NET_WaitUntilConnected(sock, -1) == NET_FAILURE) {
        SDL_Log("Client: connection failed: %s", SDL_GetError());
        NET_DestroyStreamSocket(sock);
        sock = nullptr;
        return false;
    }

    msgStream.socket = sock;

    SDL_Log("Client created, server address is %s", NET_GetAddressString(serverAddr));

    // Stage 3c: spawn the network thread once everything's ready. It owns
    // kernel I/O for this connection from here onward; the game thread
    // pushes/pulls via the locked outbound_/recvBuf path.
    shouldStop_.store(false, std::memory_order_relaxed);
    socketDead_.store(false, std::memory_order_relaxed);
    networkThread_ = std::thread(&Client::networkLoop, this);

    return true;
}

void Client::shutdown()
{
    // Stop and join the network thread first so it doesn't race the
    // socket teardown below.
    shouldStop_.store(true, std::memory_order_relaxed);
    if (networkThread_.joinable()) {
        networkThread_.join();
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (msgStream.socket) {
        NET_DestroyStreamSocket(msgStream.socket);
        msgStream.socket = nullptr;
    }

    if (serverAddr) {
        NET_UnrefAddress(serverAddr);
        serverAddr = nullptr;
    }

    outbound_.clear();
}

// ── Stage 3c: framing helper (mirrors Server.cpp's frameMessage) ─────────
namespace
{
std::vector<uint8_t> frameMessage(const void* data, uint32_t len)
{
    std::vector<uint8_t> framed(sizeof(Uint32) + len);
    const Uint32 msgLen = len;
    std::memcpy(framed.data(), &msgLen, sizeof(msgLen));
    std::memcpy(framed.data() + sizeof(msgLen), data, len);
    return framed;
}
} // namespace

bool Client::send(const void* data, uint32_t len)
{
    // Frame outside the lock; lock briefly to push into the outbound queue.
    // The network thread will drain it to the socket within ~1 ms.
    auto framed = frameMessage(data, len);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        // replaceKey 0 = always append, never drop on age. For a client with
        // a single connection there's no head-of-line concern between
        // packets here (the kernel does fair queueing), and the volumes
        // are tiny — INPUT every tick (~200 B), PING every second (~10 B).
        // Stage 3c keeps it simple and reliable.
        outbound_.enqueue(0, std::move(framed));
    }

    stats.bytesSentTotal += len + 4; // +4 for length prefix
    bytesSentWindow += len + 4;
    return !socketDead_.load(std::memory_order_relaxed);
}

void Client::sendPing()
{
    Uint64 now = SDL_GetPerformanceCounter();
    uint8_t buf[1 + sizeof(Uint64)];
    buf[0] = static_cast<uint8_t>(PacketType::PING);
    std::memcpy(buf + 1, &now, sizeof(Uint64));
    send(buf, sizeof(buf));
}

void Client::updateStats(float dt)
{
    statsAccumulator += dt;
    if (statsAccumulator >= 1.0f) {
        stats.recvBytesPerSec = static_cast<float>(bytesRecvWindow) / statsAccumulator;
        stats.sendBytesPerSec = static_cast<float>(bytesSentWindow) / statsAccumulator;
        stats.registryUpdatesPerSec = static_cast<float>(registryUpdatesWindow) / statsAccumulator;
        bytesRecvWindow = 0;
        bytesSentWindow = 0;
        registryUpdatesWindow = 0;
        statsAccumulator = 0.0f;
    }
}

bool Client::sendInputSnapshot(const InputSnapshot& snap)
{
    // Push the new snapshot into the ring (chronological).
    inputRing_[inputRingHead_] = snap;
    inputRingHead_ = (inputRingHead_ + 1) % k_inputRedundancy;
    if (inputRingCount_ < k_inputRedundancy)
        ++inputRingCount_;

    // Pack [PacketType::INPUT (1B)] [count (1B)] [InputSnapshot * count].
    // Inputs are written oldest-first so the server can apply them in
    // tick order with a simple `tick > lastAppliedInputTick` check.
    const auto count = static_cast<uint8_t>(inputRingCount_);
    uint8_t buf[2 + k_inputRedundancy * sizeof(InputSnapshot)];
    buf[0] = static_cast<uint8_t>(PacketType::INPUT);
    buf[1] = count;

    // Oldest-first iteration: when ring is full, oldest is at head; otherwise
    // entries [0, count) are already in order.
    const size_t firstIdx = (inputRingCount_ == k_inputRedundancy) ? inputRingHead_ : 0;
    for (size_t i = 0; i < inputRingCount_; ++i) {
        const size_t srcIdx = (firstIdx + i) % k_inputRedundancy;
        std::memcpy(buf + 2 + i * sizeof(InputSnapshot), &inputRing_[srcIdx], sizeof(InputSnapshot));
    }

    const uint32_t totalLen = 2 + count * static_cast<uint32_t>(sizeof(InputSnapshot));
    return send(buf, totalLen);
}

float Client::getSnapshotAlpha() const
{
    // Phase 5a: render-time interpolation alpha based on snapshot timing.
    // Returns 1.0 (snap-to-current, no interpolation reference) until two
    // snapshots have arrived, since we need the prior snapshot to know
    // when "the start of the current interval" was.
    if (lastSnapshotApplyNs_ == 0 || prevSnapshotApplyNs_ == 0)
        return 1.0f;

    const Uint64 now = SDL_GetTicksNS();
    const Uint64 elapsed = now - lastSnapshotApplyNs_;
    const Uint64 interval = lastSnapshotApplyNs_ - prevSnapshotApplyNs_;

    if (interval == 0)
        return 1.0f;

    // Clamp to [0, 1]: alpha goes 0 just after a snapshot, climbs toward 1
    // as the next is overdue, freezes at 1 (== "snap to current pos") if
    // the next snapshot is late. We never extrapolate past the latest
    // snapshot — per the Phase-5 plan, freeze is less ugly than rubber-
    // banding when packets are late.
    const float a = static_cast<float>(elapsed) / static_cast<float>(interval);
    return std::clamp(a, 0.0f, 1.0f);
}

void Client::networkLoop()
{
    // ~1 kHz cycle, symmetric to Server::networkLoop. Each phase takes the
    // mutex briefly so the game thread's enqueue / poll calls don't have
    // to wait for an entire I/O round-trip.
    while (!shouldStop_.load(std::memory_order_relaxed)) {
        // Pump kernel reads into recvBuf.
        bool socketOk = true;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (msgStream.socket) {
                socketOk = msgStream.pumpReads();
            }
        }

        if (!socketOk) {
            // Latch the socket-dead flag so the game thread's next poll()
            // returns false and the gameplay layer can disconnect cleanly.
            socketDead_.store(true, std::memory_order_relaxed);
            // Stop pumping until shutdown — there's nothing more to read.
            // We still keep the thread alive so shouldStop_ joins cleanly.
            SDL_Delay(10);
            continue;
        }

        // Drain the outbound queue.
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (msgStream.socket) {
                // 0 = no max-age culling: client outbound is small (input
                // packets, pings) and reliable-style. The server-side queue
                // is the one that needs replace-on-stale.
                if (!outbound_.flushTo(msgStream.socket, 0)) {
                    socketDead_.store(true, std::memory_order_relaxed);
                }
            }
        }

        SDL_Delay(1);
    }
}

void Client::dispatchMessage(const uint8_t* data, Uint32 size, Registry& registry)
{
    if (size < 1)
        return;
    auto type = static_cast<PacketType>(data[0]);
    const uint8_t* payload = data + 1;
    uint32_t payloadSize = size - 1;

    switch (type) {
    case PacketType::ASSIGN_CLIENT_ID: {
        if (payloadSize != sizeof(entt::entity)) {
            SDL_Log("Client: received ASSIGN_CLIENT_ID packet of invalid size %u (expected %zu)",
                    payloadSize,
                    sizeof(entt::entity));
            return;
        }
        entt::entity assignedEntity;
        std::memcpy(&assignedEntity, payload, sizeof(entt::entity));
        localPlayerEntity = assignedEntity;
        break;
    }
    case PacketType::UPDATE_REGISTRY:
        // Phase 5a: copy current Position into PreviousPosition BEFORE the
        // continuous_loader rewrites Position from the snapshot. This gives
        // the renderer a (prev, pos) pair that brackets the most-recent
        // snapshot transition, lerped over the full ~31 ms snapshot
        // interval (vs. the old per-physics-tick lerp which only spanned
        // ~7.8 ms and produced visible step jitter at 32 Hz snapshot rate).
        registry.view<Position, PreviousPosition>().each(
            [](const Position& pos, PreviousPosition& prev) { prev.value = pos.value; });

        if (!registryLoader)
            registryLoader.emplace(registry);
        registryLoader->apply(payload, payloadSize, localPlayerEntity);

        // Newly-spawned entities (created by the snapshot apply above) have
        // a default-constructed PreviousPosition (or none at all). Without
        // this seed pass, the very first frame would lerp them from (0,0,0)
        // to their actual spawn position — visible "fly in from origin".
        // Setting prev = pos for any entity missing PreviousPosition pins
        // them at the snapshot pos until the *next* snapshot creates a
        // real motion delta.
        registry.view<Position>(entt::exclude<PreviousPosition>).each([&registry](entt::entity e, const Position& pos) {
            registry.emplace<PreviousPosition>(e, pos.value);
        });

        // Update snapshot timing for the renderer's interpolation alpha.
        prevSnapshotApplyNs_ = lastSnapshotApplyNs_;
        lastSnapshotApplyNs_ = SDL_GetTicksNS();

        stats.registryUpdateSize = payloadSize;
        ++registryUpdatesWindow;

        // call the local player ready callback once we have the registry update that includes the local player
        if (!localPlayerReadyNotified && localPlayerEntity && localPlayerReadyFn) {
            auto local = registryLoader->map(*localPlayerEntity);
            if (local != entt::null) {
                localPlayerReadyFn(local);
                localPlayerReadyNotified = true;
            }
        }
        break;
    case PacketType::PARTICLE_SPAWN: {
        if (payloadSize < sizeof(uint32_t))
            break;
        uint32_t count = 0;
        std::memcpy(&count, payload, sizeof(uint32_t));
        const uint8_t* eventData = payload + sizeof(uint32_t);
        const uint32_t expectedSize = sizeof(uint32_t) + count * sizeof(NetParticleEvent);
        if (payloadSize < expectedSize)
            break;

        if (particleEventFn_ && registryLoader && localPlayerEntity) {
            entt::entity localE = registryLoader->map(*localPlayerEntity);
            for (uint32_t i = 0; i < count; ++i) {
                NetParticleEvent evt;
                std::memcpy(&evt, eventData + i * sizeof(NetParticleEvent), sizeof(NetParticleEvent));
                // Map server entities to client entities
                evt.source = registryLoader->map(evt.source);
                if (evt.target != entt::null)
                    evt.target = registryLoader->map(evt.target);
                particleEventFn_(evt, localE);
            }
        }
        break;
    }
    case PacketType::PONG: {
        if (payloadSize != sizeof(Uint64))
            break;
        Uint64 sendTime;
        std::memcpy(&sendTime, payload, sizeof(Uint64));
        Uint64 now = SDL_GetPerformanceCounter();
        float rtt = static_cast<float>(now - sendTime) / static_cast<float>(SDL_GetPerformanceFrequency()) * 1000.0f;
        stats.rttMs = rtt;
        // Exponential moving average (alpha = 0.2)
        if (stats.avgRttMs <= 0.0f)
            stats.avgRttMs = rtt;
        else
            stats.avgRttMs = stats.avgRttMs * 0.8f + rtt * 0.2f;
        break;
    }
    case PacketType::MATCH_STATE: {
        if (payloadSize != sizeof(MatchStatePacket))
            break;
        MatchStatePacket matchState;
        std::memcpy(&matchState, payload, sizeof(MatchStatePacket));
        if (matchStateUpdateFn_)
            matchStateUpdateFn_(matchState);
        break;
    }
    case PacketType::KILL_EVENT: {
        if (payloadSize < sizeof(uint32_t))
            break;
        uint32_t count = 0;
        std::memcpy(&count, payload, sizeof(uint32_t));
        const uint8_t* eventData = payload + sizeof(uint32_t);
        const uint32_t expectedSize = sizeof(uint32_t) + count * sizeof(NetKillEvent);
        if (payloadSize < expectedSize)
            break;
        if (killEventFn_) {
            for (uint32_t i = 0; i < count; ++i) {
                NetKillEvent evt;
                std::memcpy(&evt, eventData + i * sizeof(NetKillEvent), sizeof(NetKillEvent));
                killEventFn_(evt);
            }
        }
        break;
    }
    default:
        SDL_Log("Client: unknown message type %d", static_cast<int>(type));
        break;
    }
}

bool Client::poll(Registry& registry)
{
    // Stage 3c: the network thread is already pumping reads into recvBuf
    // and writing the outbound queue. poll's job is just (a) check if the
    // socket has died, (b) copy any complete framed messages out of recvBuf
    // under the mutex, then (c) dispatch them outside the mutex (the
    // dispatch touches the Registry which is exclusively the game thread's
    // domain).

    if (socketDead_.load(std::memory_order_relaxed)) {
        SDL_Log("Client: server dead");
        return false;
    }

    // Snapshot complete framed messages out of recvBuf with the lock held.
    // We hold the lock only for the byte copy; dispatch happens after the
    // unlock so registry.apply / entt::continuous_loader can take their
    // time without blocking the network thread's read pump.
    std::vector<std::vector<uint8_t>> ready;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        msgStream.drainComplete([&](const void* data, Uint32 size) {
            ready.emplace_back(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        });
    }

    // Dispatch (lock-free).  Stats are touched here only — they're not
    // cross-thread shared (only this game thread reads/writes them).
    for (const auto& msg : ready) {
        stats.bytesRecvTotal += msg.size() + 4; // +4 for the length prefix that drainComplete already stripped
        bytesRecvWindow += msg.size() + 4;
        dispatchMessage(msg.data(), static_cast<Uint32>(msg.size()), registry);
    }

    return true;
}
