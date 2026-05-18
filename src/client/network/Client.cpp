/// @file Client.cpp
/// @brief Implementation of the TCP client connection and message I/O.

#include "Client.hpp"

#include "EntityInterpolation.hpp"
#include "ecs/components/InterpolationBuffer.hpp"
#include "ecs/components/LocalPlayer.hpp"
#include "ecs/components/PlayerVisState.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/PreviousPosition.hpp"
#include "ecs/components/Velocity.hpp"
#include "network/MatchStatus.hpp"
#include "network/NetKillEvent.hpp"
#include "network/PacketType.hpp"
#include "network/RegistrySerialization.hpp"
#include "network/lobby/LobbyStatus.hpp"
#include "network/transport/PacketHeader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <cmath>
#include <cstring>

ConnectError Client::init(const char* addr,
                          Uint16 port,
                          const TransportConfig& transport,
                          int timeoutMs,
                          const std::optional<net::UdpSessionTransport::RelayConfig>& relay)
{
    transportConfig_ = transport;

    if (const char* envDelay = SDL_getenv("GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS")) {
        const int parsed = SDL_atoi(envDelay);
        interpDelaySnapshots_ = std::clamp(parsed, 0, static_cast<int>(InterpolationBuffer::k_capacity));
        SDL_Log("Client: GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS=%d", interpDelaySnapshots_);
    }
    simLossRng_.seed(std::random_device{}());

    if (transportConfig_.useUdpSessions) {
        usingUdpSession_ = true;
        session_.preferRelay(transportConfig_.forceRelay);
        if (relay)
            session_.setRelayConfig(*relay);
        if (!session_.connectClient(addr, port, timeoutMs)) {
            SDL_Log("Client: UDP session connection to %s:%u failed", addr, port);
            session_.close();
            usingUdpSession_ = false;
            return ConnectError::ConnectTimedOut;
        }

        connectionId_ = session_.clientConnectionId();
        SDL_Log("Client: UDP session connected to %s:%u, session=0x%llx",
                addr,
                port,
                static_cast<unsigned long long>(connectionId_));

        shouldStop_.store(false, std::memory_order_relaxed);
        socketDead_.store(false, std::memory_order_relaxed);
        networkThread_ = std::thread(&Client::networkLoop, this);
        return ConnectError::None;
    }

    usingUdpSession_ = false;
    serverAddr = NET_ResolveHostname(addr);
    if (!serverAddr) {
        SDL_Log("Failed to start resolving server address: %s", SDL_GetError());
        return ConnectError::ResolveFailed;
    }

    const NET_Status resolveStatus = NET_WaitUntilResolved(serverAddr, timeoutMs);
    if (resolveStatus != NET_SUCCESS) {
        SDL_Log("Failed to resolve server address: %s", SDL_GetError());
        NET_UnrefAddress(serverAddr);
        serverAddr = nullptr;
        return resolveStatus == NET_WAITING ? ConnectError::ResolveTimedOut : ConnectError::ResolveFailed;
    }

    auto sock = NET_CreateClient(serverAddr, port);
    if (!sock) {
        SDL_Log("Failed to create client %s", SDL_GetError());
        NET_UnrefAddress(serverAddr);
        serverAddr = nullptr;
        return ConnectError::CreateClientFailed;
    }
    NET_SetStreamSocketNoDelay(sock, true);

    const NET_Status connectStatus = NET_WaitUntilConnected(sock, timeoutMs);
    if (connectStatus != NET_SUCCESS) {
        SDL_Log("Client: connection failed: %s", SDL_GetError());
        NET_DestroyStreamSocket(sock);
        sock = nullptr;
        NET_UnrefAddress(serverAddr);
        serverAddr = nullptr;
        return connectStatus == NET_WAITING ? ConnectError::ConnectTimedOut : ConnectError::ConnectFailed;
    }

    msgStream.socket = sock;
    transportConfig_ = transport;

    // PR-11: read GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS once at connect.
    // Default 2 ticks (≈ 62.5 ms at 32 Hz snapshot rate) — Source-engine
    // / Valorant / Fortnite cadence.  0 disables the buffered render-
    // delay path entirely; remote-entity rendering falls back to the
    // Phase-5a `(prev, cur, alpha)` lerp.  Clamp to a sane upper bound
    // (8 ticks ≈ 250 ms at 32 Hz, the InterpolationBuffer capacity) so
    // a typo in the env var can't push the renderer past the buffer's
    // history depth, which would have it permanently snapped to oldest.
    if (const char* envDelay = SDL_getenv("GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS")) {
        const int parsed = SDL_atoi(envDelay);
        interpDelaySnapshots_ = std::clamp(parsed, 0, static_cast<int>(InterpolationBuffer::k_capacity));
        SDL_Log("Client: GROUP2_CLIENT_INTERP_DELAY_SNAPSHOTS=%d", interpDelaySnapshots_);
    }

    // Seed the loss simulator RNG once. random_device for fresh entropy
    // across runs; deterministic per-session so loss patterns are at
    // least repeatable within a single test run.
    simLossRng_.seed(std::random_device{}());

    SDL_Log("Client created, server address is %s", NET_GetAddressString(serverAddr));

    // ── Phase 3d-1: open UDP sidecar ─────────────────────────────────────
    //
    // Bind to any free local port (kernel picks). Server's address is
    // the same one we resolved above; we hold a refcounted copy in
    // serverUdpAddr_ for use in NET_SendDatagram. UDP traffic only
    // starts after the server gives us a connectionId via the
    // ASSIGN_CLIENT_ID TCP packet (see dispatchMessage).
    if (transportConfig_.enableUdpSidecar) {
        if (udpEndpoint_.open(/*bindAddr*/ nullptr, /*port*/ 0)) {
            serverUdpAddr_.addr = NET_RefAddress(serverAddr);
            serverUdpAddr_.port = port;
            SDL_Log("Client: UDP sidecar opened, server %s:%u", NET_GetAddressString(serverAddr), port);
        } else {
            SDL_Log("Client: UDP sidecar open failed; falling back to TCP-only");
        }
    }

    // Stage 3c: spawn the network thread once everything's ready. It owns
    // kernel I/O for this connection from here onward; the game thread
    // pushes/pulls via the locked outbound_/recvBuf path.
    shouldStop_.store(false, std::memory_order_relaxed);
    socketDead_.store(false, std::memory_order_relaxed);
    networkThread_ = std::thread(&Client::networkLoop, this);

    return ConnectError::None;
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
    session_.close();
    usingUdpSession_ = false;
    if (msgStream.socket) {
        NET_DestroyStreamSocket(msgStream.socket);
        msgStream.socket = nullptr;
    }

    udpEndpoint_.close();
    serverUdpAddr_.release();

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
    if (usingUdpSession_) {
        const bool ok =
            session_.send(connectionId_, net::ChannelId::ControlReliableOrdered, data, static_cast<int>(len));
        if (ok) {
            stats.bytesSentTotal += len + sizeof(net::PacketHeader);
            bytesSentWindow += len + sizeof(net::PacketHeader);
        }
        return ok;
    }

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

    if (usingUdpSession_) {
        session_.send(connectionId_, net::ChannelId::InputUnreliable, buf, static_cast<int>(sizeof(buf)));
        return;
    }

    // ── Phase 3d-3: prefer UDP for PING ─────────────────────────────────
    //
    // PING/PONG over UDP gives much truer RTT readings: the PONG byte
    // can't get queued behind a snapshot in a TCP stream (the original
    // "ping decay on join" symptom from before Phase 1). On UDP each
    // datagram lands independently, so PONG always reflects the real
    // network round-trip time. Loss is fine — PINGs go out once a
    // second; missing one just delays the next sample.
    if (transportConfig_.pingOverUdp && udpEndpoint_.isOpen() && connectionId_ != 0 && serverUdpAddr_.addr) {
        net::PacketHeader hdr{};
        hdr.kind = static_cast<uint8_t>(net::PacketKind::Payload);
        hdr.connectionId = connectionId_;
        hdr.sequence = 0; // PING sequence not used yet — server just echoes payload
        hdr.channel = static_cast<uint8_t>(net::ChannelId::Unreliable);
        // Payload is the raw [PacketType::PING][timestamp] bytes — server
        // demuxes on channel + first payload byte. `sendUdpDelayed` either
        // ships immediately (latency simulator off) or queues for the
        // network thread's drain phase. Bandwidth accounting moves with it
        // so deferred sends still register in `bytesSentTotal` once they
        // actually leave the kernel.
        std::lock_guard<std::mutex> lock(stateMutex_);
        const int totalMs = simulatedLatencyMs_.load(std::memory_order_relaxed);
        if (sendUdpDelayed(hdr, buf, static_cast<int>(sizeof(buf)))) {
            if (totalMs == 0) {
                stats.bytesSentTotal += sizeof(net::PacketHeader) + sizeof(buf);
                bytesSentWindow += sizeof(net::PacketHeader) + sizeof(buf);
            }
            return;
        }
        // Fall through to TCP if UDP send failed.
    }
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

    // Pack [PacketType::INPUT (1B)] [count (1B)] [rttMs (2B)]
    //      [interpDelaySnapshots (1B)] [InputSnapshot * count].
    //
    // Inputs are written oldest-first so the server can apply them in
    // tick order with a simple `tick > lastAppliedInputTick` check.
    //
    // Phase 6: the `rttMs` header carries the client's smoothed RTT
    // estimate (avgRttMs from the PING/PONG flow). The server uses
    // RTT/2 as the rewind target for lag-compensated hitscan. It's
    // sent on every input packet — at 128 Hz this overpays in bytes
    // slightly (2 B × 128 Hz = 256 B/s), but it means the server's
    // rewind window adapts to ping changes within ~one tick instead
    // of waiting for a separate "ping update" packet to arrive on
    // its own channel. Rounded to the nearest ms; clamped to uint16
    // (0–65 s, far beyond the 200 ms cap the server applies).
    //
    // PR-12: the `interpDelaySnapshots` byte carries the client's
    // render-delay value (PR-11 entity-interpolation). The server
    // ADDS this term to RTT/2 in its lag-comp formula so the rewind
    // target tick lines up with what the client *saw* on screen at
    // input-sample time, not merely with what the server held at
    // input-arrival time. Without this, players at higher cl_interp
    // experience shots-behind-target — the server hits where the
    // enemy was 50 ms ago (RTT/2) but the client aimed at where the
    // enemy was 50 + 62.5 ms ago. One byte / packet — same
    // negligible cost as rttMs.
    const auto count = static_cast<uint8_t>(inputRingCount_);
    const float rttClamped = std::clamp(stats.avgRttMs, 0.0f, 65535.0f);
    const auto rttMs = static_cast<uint16_t>(std::lround(rttClamped));
    const auto interpDelaySnapshots =
        static_cast<uint8_t>(std::clamp(interpDelaySnapshots_, 0, static_cast<int>(InterpolationBuffer::k_capacity)));
    uint8_t buf[5 + k_inputRedundancy * sizeof(InputSnapshot)];
    buf[0] = static_cast<uint8_t>(PacketType::INPUT);
    buf[1] = count;
    std::memcpy(buf + 2, &rttMs, sizeof(uint16_t));
    buf[4] = interpDelaySnapshots;

    // Oldest-first iteration: when ring is full, oldest is at head; otherwise
    // entries [0, count) are already in order.
    const size_t firstIdx = (inputRingCount_ == k_inputRedundancy) ? inputRingHead_ : 0;
    for (size_t i = 0; i < inputRingCount_; ++i) {
        const size_t srcIdx = (firstIdx + i) % k_inputRedundancy;
        std::memcpy(buf + 5 + i * sizeof(InputSnapshot), &inputRing_[srcIdx], sizeof(InputSnapshot));
    }

    const uint32_t totalLen = 5 + count * static_cast<uint32_t>(sizeof(InputSnapshot));

    if (usingUdpSession_) {
        return session_.send(connectionId_, net::ChannelId::InputUnreliable, buf, static_cast<int>(totalLen));
    }

    // ── Phase 3d-2: prefer UDP for INPUT once handshake completes ────────
    //
    // INPUT is naturally loss-tolerant (5-tick redundancy means a single
    // dropped datagram is recovered from the next packet's history).
    // Routing it over UDP gets it off the snapshot stream entirely —
    // no head-of-line blocking against the much larger registry packet.
    //
    // Fall back to TCP when:
    //   (a) UDP sidecar isn't enabled in config, or
    //   (b) Server hasn't given us a connectionId yet (pre-handshake).
    // The TCP path is functionally identical (Phase 3a/b queue + drain).
    if (transportConfig_.inputsOverUdp && udpEndpoint_.isOpen() && connectionId_ != 0 && serverUdpAddr_.addr) {
        // UDP wire format mirrors TCP: `[PacketType][rest of payload]`.
        // The PacketHeader's `channel` field selects reliability
        // semantics (Unreliable here for INPUT — drop-stale, no
        // retransmit), but the type byte stays for protocol uniformity
        // with the TCP path. Server's handleUdpUnreliable dispatches
        // on the type byte just like TCP's handleMessage does.
        net::PacketHeader hdr{};
        hdr.kind = static_cast<uint8_t>(net::PacketKind::Payload);
        hdr.connectionId = connectionId_;
        hdr.sequence = udpInputSequence_++;
        hdr.channel = static_cast<uint8_t>(net::ChannelId::Unreliable);

        std::lock_guard<std::mutex> lock(stateMutex_);
        const int totalMs = simulatedLatencyMs_.load(std::memory_order_relaxed);
        if (sendUdpDelayed(hdr, buf, static_cast<int>(totalLen))) {
            if (totalMs == 0) {
                stats.bytesSentTotal += sizeof(net::PacketHeader) + totalLen;
                bytesSentWindow += sizeof(net::PacketHeader) + totalLen;
            }
            return true;
        }
        // Fall through to TCP if UDP send failed (rare).
    }

    return send(buf, totalLen);
}

bool Client::sendShotIntent(std::uint32_t shotInputTick, std::uint16_t targetClientId, const AnimSnapshot& targetAnim)
{
    // PR-27 wire format:
    //   [PacketType::SHOT_INTENT : u8]
    //   [shotInputTick           : u32]
    //   [targetClientId          : u16]
    //   [AnimSnapshot 5×4-byte slots = 20B]
    // Total payload = 27 bytes.  Sent on UDP unreliable channel
    // alongside INPUT (PR-27 only fires on rising edge of shooting,
    // ≤ ~10 Hz worst case → ~270 B/s/client).
    constexpr std::size_t animBytes = anim_snapshot::k_wireSize;
    constexpr std::size_t payloadLen = 1 + sizeof(uint32_t) + sizeof(uint16_t) + animBytes;
    static_assert(payloadLen == 27, "SHOT_INTENT wire size must equal 27 bytes");

    uint8_t buf[payloadLen];
    buf[0] = static_cast<uint8_t>(PacketType::SHOT_INTENT);
    std::memcpy(buf + 1, &shotInputTick, sizeof(uint32_t));
    std::memcpy(buf + 1 + sizeof(uint32_t), &targetClientId, sizeof(uint16_t));
    anim_snapshot::packSnapshot(targetAnim, buf + 1 + sizeof(uint32_t) + sizeof(uint16_t));

    if (usingUdpSession_) {
        return session_.send(connectionId_, net::ChannelId::InputUnreliable, buf, static_cast<int>(payloadLen));
    }

    // SHOT_INTENT prefers UDP for the same reasons as INPUT — single-
    // shot packets are loss-tolerant: a missed SHOT_INTENT just means
    // the server falls back to its own historical anim state for that
    // shot, which is the pre-PR-27 behaviour.  No retransmit, no
    // bandwidth cost on TCP.
    if (transportConfig_.inputsOverUdp && udpEndpoint_.isOpen() && connectionId_ != 0 && serverUdpAddr_.addr) {
        net::PacketHeader hdr{};
        hdr.kind = static_cast<uint8_t>(net::PacketKind::Payload);
        hdr.connectionId = connectionId_;
        hdr.sequence = udpInputSequence_++; // share INPUT's sequence space — both unreliable
        hdr.channel = static_cast<uint8_t>(net::ChannelId::Unreliable);

        std::lock_guard<std::mutex> lock(stateMutex_);
        const int totalMs = simulatedLatencyMs_.load(std::memory_order_relaxed);
        if (sendUdpDelayed(hdr, buf, static_cast<int>(payloadLen))) {
            if (totalMs == 0) {
                stats.bytesSentTotal += sizeof(net::PacketHeader) + payloadLen;
                bytesSentWindow += sizeof(net::PacketHeader) + payloadLen;
            }
            return true;
        }
    }

    return send(buf, payloadLen);
}

bool Client::sendChatMessage(std::string_view message)
{
    std::vector<std::uint8_t> payload = net::chat::encodeClientText(chatClientSeq_++, message);
    if (payload.empty())
        return false;
    return send(payload.data(), static_cast<std::uint32_t>(payload.size()));
}

bool Client::sendVoiceFrame(std::uint16_t sequence, std::uint8_t frameMs, std::span<const std::uint8_t> opus)
{
    if (!usingUdpSession_)
        return false;

    std::vector<std::uint8_t> payload = net::voice::encodeClientFrame(sequence, frameMs, opus);
    if (payload.empty())
        return false;

    return session_.send(
        connectionId_, net::ChannelId::VoiceUnreliableSequenced, payload.data(), static_cast<int>(payload.size()));
}

bool Client::sendPlayerReady(bool ready)
{
    const auto type = static_cast<uint8_t>(ready ? PacketType::PLAYER_READY : PacketType::PLAYER_UNREADY);
    return send(&type, sizeof(type));
}

bool Client::sendStartMatch()
{
    const auto type = static_cast<uint8_t>(PacketType::START_MATCH);
    return send(&type, sizeof(type));
}

std::optional<std::pair<std::vector<LobbyPlayer>, ClientId>> Client::getLatestLobbyState() const
{
    if (!latestLobbyPlayers_ || !latestLobbyLocalId_)
        return std::nullopt;

    return std::make_pair(*latestLobbyPlayers_, *latestLobbyLocalId_);
}

bool Client::acceptReliableSequence(std::uint32_t seq)
{
    // First sequence ever — accept and seed the window.
    if (!reliableHasAny_) {
        reliableHasAny_ = true;
        reliableHighestSeen_ = seq;
        reliableSeenBitmask_ = 1; // bit 0 = highest-seen marker
        return true;
    }

    // Glenn-Fiedler distance with 16-bit wrap. Positive forward delta
    // means `seq` is newer than highestSeen.
    const std::uint32_t fwd = seq - reliableHighestSeen_;
    if (fwd != 0 && fwd < 0x80000000u) {
        // Newer: shift the window forward by `fwd` bits, set bit 0
        // (current = highest-seen), accept.
        reliableSeenBitmask_ = (fwd >= 64) ? 0 : (reliableSeenBitmask_ << fwd);
        reliableSeenBitmask_ |= 1ULL;
        reliableHighestSeen_ = seq;
        return true;
    }
    if (fwd == 0) {
        // Same as highestSeen → duplicate.
        return false;
    }

    // Older. distance = how many slots behind the highest.
    const std::uint32_t back = reliableHighestSeen_ - seq;
    if (back >= 64) {
        // Too old for the window — assume duplicate / out-of-order
        // beyond redundancy budget.
        return false;
    }
    const uint64_t bit = 1ULL << back;
    if (reliableSeenBitmask_ & bit)
        return false; // already seen
    reliableSeenBitmask_ |= bit;
    return true;
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

Uint64 Client::getSnapshotIntervalNs() const
{
    return snapshotIntervalEmaNs_;
}

void Client::applyInterpolatedTransforms(Registry& registry)
{
    // PR-19: opt-out semantics — disabled if cl_interp = 0 or no
    // buffered playback yet.  In both cases the registry's `pos.value`
    // already holds the server-authoritative state from the most
    // recent snapshot apply, which is the right thing for consumers
    // to see (Phase-5a behaviour).
    if (interpDelaySnapshots_ <= 0)
        return;
    const Uint64 renderTimeNs = getInterpolationRenderTimeNs();
    if (renderTimeNs == 0)
        return;

    // Walk every entity that has an InterpolationBuffer (created by
    // recordInterpolationSamples for non-local entities only — see
    // that function for the LocalPlayer exclude).  Sample each buffer
    // at the shared render-time and overwrite Position + yaw in place.
    //
    // PR-20.7 (defensive): explicit `exclude<LocalPlayer>` here even
    // though `recordInterpolationSamples` already filters local out.
    // Belt-and-suspenders for the corner case where the local player
    // somehow ended up with an InterpolationBuffer (server-side
    // entity re-creation across respawn, packet ordering during the
    // ASSIGN_CLIENT_ID → first-snapshot transition, etc.) — without
    // this exclude the local player's `pos.value` would get
    // overwritten with a 16 ms-stale interpolated value every frame,
    // visibly fighting client-side prediction.  The user reported
    // movement jitter at 30 ms simulated RTT; this guards against
    // that path even if the original sample-side filter slipped.
    auto view = registry.view<Position, InterpolationBuffer>(entt::exclude<LocalPlayer>);
    for (const auto e : view) {
        auto& pos = view.get<Position>(e);
        // Yaw fallback comes from the entity's own InputSnapshot if
        // present; otherwise 0.  The sample() helper internally
        // returns the fallback when the buffer can't bracket the
        // requested time, so unbuffered cases preserve current state.
        float fallbackYaw = 0.0f;
        InputSnapshot* snap = registry.try_get<InputSnapshot>(e);
        if (snap != nullptr)
            fallbackYaw = snap->yaw;

        const auto sampled = entity_interpolation::sample(registry, e, renderTimeNs, pos.value, fallbackYaw);
        pos.value = sampled.position;
        if (snap != nullptr) {
            snap->yaw = sampled.yaw;
            // PR-28: also write back interp-delayed pitch.  Animator
            // and renderer both read pitch from `InputSnapshot` for
            // remote players (head/neck bone tilt).
            if (sampled.fromBuffer)
                snap->pitch = sampled.pitch;
        }

        // PR-28: also write back interp-delayed Velocity and the
        // animator-relevant PlayerVisState bits.  Pre-PR-28 these
        // were left at their LATEST-snapshot values, which preceded
        // the body's interp-delayed Position by `cl_interp ×
        // snapshotInterval`.  At 30+ ms that produced the locomotion-
        // state mismatch PR-27a measured (0.41-median anim-state
        // delta).  Lag-comp (server-side) is unaffected — the server
        // continues to read its OWN live state, never these client-
        // mutated values.
        if (sampled.fromBuffer) {
            if (auto* vel = registry.try_get<Velocity>(e))
                vel->value = sampled.velocity;
            if (auto* ps = registry.try_get<PlayerVisState>(e)) {
                ps->moveMode = static_cast<MoveMode>(sampled.moveMode);
                ps->wallRunSide = static_cast<WallSide>(sampled.wallRunSide);
                ps->grounded = sampled.grounded;
                ps->sprinting = sampled.sprinting;
                ps->crouching = sampled.crouching;
            }
            // PR-29: write back interp-delayed `AnimSnapshot` so the
            // renderer's `CharacterAnimator::renderFromServer(...)`
            // call picks up server-authoritative animation state at
            // the same body-render-time the position is at.  Without
            // this, the buffered timeRatio would only be available on
            // entity's "live" component (snapshot-latest) and the
            // animator would render the body at "now" rather than
            // "now - cl_interp", reintroducing the drift PR-29 is
            // here to eliminate.
            if (auto* anim = registry.try_get<AnimSnapshot>(e))
                *anim = sampled.anim;
        }
    }
}

void Client::recordInterpolationSamples(Registry& registry, Uint64 captureNs)
{
    if (interpDelaySnapshots_ <= 0)
        return;

    // PR-28: capture position + animator inputs for every replicated
    // non-local entity.  Pre-PR-28 only `(position, yaw)` were buffered;
    // the animator continued to read LATEST-snapshot Velocity / pitch /
    // PlayerVisState bits while the body rendered at `now − cl_interp`,
    // so locomotion-state transitions visibly preceded the body motion.
    // PR-27a's telemetry caught the resulting 0.41-median anim-state
    // delta.  Buffering the animator inputs here lines body and pose up
    // at the same logical instant.  Fields without a server source
    // (e.g. yaw on a projectile) default to 0 — animator never reads
    // those for non-player entities anyway.
    auto view = registry.view<Position>(entt::exclude<LocalPlayer>);
    for (const auto e : view) {
        entity_interpolation::SampleInputs inputs;
        inputs.position = view.get<Position>(e).value;
        if (const auto* vel = registry.try_get<Velocity>(e))
            inputs.velocity = vel->value;
        if (const auto* inp = registry.try_get<InputSnapshot>(e)) {
            inputs.yaw = inp->yaw;
            inputs.pitch = inp->pitch;
        }
        if (const auto* ps = registry.try_get<PlayerVisState>(e)) {
            inputs.moveMode = static_cast<std::uint8_t>(ps->moveMode);
            inputs.wallRunSide = static_cast<std::uint8_t>(ps->wallRunSide);
            inputs.grounded = ps->grounded;
            inputs.sprinting = ps->sprinting;
            inputs.crouching = ps->crouching;
        }
        // PR-29: server-authoritative animation state from the just-
        // applied snapshot.  Replicated via the Synced tuple.
        if (const auto* anim = registry.try_get<AnimSnapshot>(e))
            inputs.anim = *anim;
        entity_interpolation::appendSample(registry, e, captureNs, inputs);
    }
}

Uint64 Client::getInterpolationRenderTimeNs() const
{
    // Disabled by env var, or no buffered playback yet — caller falls
    // back to the Phase-5a (prev, cur, alpha) path.  We require two
    // snapshots before publishing a render time so the EMA has been
    // updated at least once with a real interval (otherwise we'd be
    // playing back at the wrong delay against a stale assumed 32 Hz).
    if (interpDelaySnapshots_ <= 0 || lastSnapshotApplyNs_ == 0 || prevSnapshotApplyNs_ == 0)
        return 0;

    const Uint64 now = SDL_GetTicksNS();
    const Uint64 delayNs = static_cast<Uint64>(interpDelaySnapshots_) * snapshotIntervalEmaNs_;

    // Guard against the (unlikely) case where the simulator is running
    // a delay larger than the wall clock.  Returning 0 disables the
    // path cleanly until enough time has accrued.
    if (delayNs >= now)
        return 0;

    return now - delayNs;
}

void Client::setSimulatedLatencyMs(int totalMs) noexcept
{
    // Clamp to slider range. Setting an unreasonably-high value would
    // grow the delay queues without bound at high traffic rates; the
    // 200 ms cap keeps queues bounded under normal load (~30 entries
    // for a 128 Hz INPUT stream + 32 Hz inbound snapshots).
    if (totalMs < 0)
        totalMs = 0;
    if (totalMs > 200)
        totalMs = 200;
    simulatedLatencyMs_.store(totalMs, std::memory_order_relaxed);
}

void Client::setSimulatedLossPercent(int percent) noexcept
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    simulatedLossPercent_.store(percent, std::memory_order_relaxed);
}

bool Client::shouldDropPacketLocked()
{
    const int pct = simulatedLossPercent_.load(std::memory_order_relaxed);
    if (pct <= 0)
        return false;
    if (pct >= 100)
        return true;
    // uniform_int_distribution[0, 99] → P(value < pct) == pct/100,
    // matching the slider's "% packets dropped" semantics.
    std::uniform_int_distribution<int> dist(0, 99);
    return dist(simLossRng_) < pct;
}

bool Client::sendUdpDelayed(net::PacketHeader hdr, const void* data, int len)
{
    // Caller already holds stateMutex_ — both the immediate send and
    // the queue mutation are guarded by the existing lock. No re-lock.

    // Phase 6 testing: simulated outbound packet loss. Roll before
    // anything else so the dropped packet doesn't even consume a
    // sequence/header slot. Returning `true` (optimistic success) keeps
    // the existing TCP-fallback logic in callers from triggering — the
    // simulator drops should look like clean UDP loss to upper layers,
    // not "send failed, fall back to TCP".
    if (shouldDropPacketLocked())
        return true;

    const int totalMs = simulatedLatencyMs_.load(std::memory_order_relaxed);
    if (totalMs == 0) {
        return udpEndpoint_.send(serverUdpAddr_, hdr, data, len);
    }

    // Half the slider value goes to outbound, the other half to
    // inbound — see the doc on `setSimulatedLatencyMs`.
    const int outboundMs = totalMs / 2;
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 delayCounters = perfFreq * static_cast<Uint64>(outboundMs) / 1000ULL;

    DelayedOutbound entry;
    entry.sendAtCounter = SDL_GetPerformanceCounter() + delayCounters;
    entry.header = hdr;
    entry.payload.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + len);
    entry.totalBytes = sizeof(net::PacketHeader) + static_cast<std::size_t>(len);
    simLatOutbound_.push_back(std::move(entry));
    return true;
}

void Client::recvUdpDelayed(std::vector<uint8_t>&& payload)
{
    const int totalMs = simulatedLatencyMs_.load(std::memory_order_relaxed);
    if (totalMs == 0) {
        udpRecvQueue_.emplace_back(std::move(payload));
        return;
    }

    const int inboundMs = totalMs / 2;
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    const Uint64 delayCounters = perfFreq * static_cast<Uint64>(inboundMs) / 1000ULL;

    DelayedInbound entry;
    entry.deliverAtCounter = SDL_GetPerformanceCounter() + delayCounters;
    entry.payload = std::move(payload);
    simLatInbound_.push_back(std::move(entry));
}

void Client::networkLoop()
{
    if (usingUdpSession_) {
        while (!shouldStop_.load(std::memory_order_relaxed)) {
            session_.pump();
            net::UdpSessionTransport::Event event;
            while (session_.pollEvent(event)) {
                if (event.type == net::UdpSessionTransport::EventType::Payload) {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    recvUdpDelayed(std::move(event.payload));
                } else if (event.type == net::UdpSessionTransport::EventType::Disconnected) {
                    socketDead_.store(true, std::memory_order_relaxed);
                }
            }

            const auto& s = session_.stats();
            stats.rttMs = s.rttMs;
            if (s.rttMs > 0.0f)
                stats.avgRttMs = stats.avgRttMs <= 0.0f ? s.rttMs : stats.avgRttMs * 0.8f + s.rttMs * 0.2f;

            SDL_Delay(1);
        }
        return;
    }

    // ~1 kHz cycle, symmetric to Server::networkLoop. Each phase takes the
    // mutex briefly so the game thread's enqueue / poll calls don't have
    // to wait for an entire I/O round-trip.
    while (!shouldStop_.load(std::memory_order_relaxed)) {
        // ── Phase 6 latency simulator: drain ready delayed packets ──────
        //
        // Outbound — anything whose `sendAtCounter` has passed gets
        // shipped to the kernel right now. Inbound — anything whose
        // `deliverAtCounter` has passed gets moved into `udpRecvQueue_`
        // for the game thread's next `poll`. Both queues are FIFO; the
        // sendAt/deliverAt timestamps are monotonic for any given
        // latency setting, so a simple while-front-ready loop preserves
        // order. If the user lowers the slider mid-flight, in-flight
        // entries still wait their original deadlines (acceptable —
        // the simulator is a debug aid, not an SLA).
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            const Uint64 nowCounter = SDL_GetPerformanceCounter();
            while (!simLatOutbound_.empty() && simLatOutbound_.front().sendAtCounter <= nowCounter) {
                auto& entry = simLatOutbound_.front();
                if (udpEndpoint_.isOpen() && serverUdpAddr_.addr) {
                    if (udpEndpoint_.send(
                            serverUdpAddr_, entry.header, entry.payload.data(), static_cast<int>(entry.payload.size())))
                    {
                        stats.bytesSentTotal += entry.totalBytes;
                        bytesSentWindow += entry.totalBytes;
                    }
                }
                simLatOutbound_.pop_front();
            }
            while (!simLatInbound_.empty() && simLatInbound_.front().deliverAtCounter <= nowCounter) {
                udpRecvQueue_.emplace_back(std::move(simLatInbound_.front().payload));
                simLatInbound_.pop_front();
            }
        }

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

        // ── Phase 3d: UDP receive phase ─────────────────────────────────
        //
        // Non-fragmented payloads (PONG, small snapshots) go straight
        // into udpRecvQueue_ for the game thread to dispatch. Fragmented
        // payloads (Phase 3d-4 large snapshots) are fed to the
        // reassembler; only the *complete* assembled message lands in
        // the queue.
        if (udpEndpoint_.isOpen()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            net::UdpReceivedMessage msg;
            int drained = 0;
            constexpr int k_maxDatagramsPerCycle = 256;
            while (drained < k_maxDatagramsPerCycle && udpEndpoint_.tryReceive(msg)) {
                ++drained;
                if (msg.header.connectionId != connectionId_ || msg.payload.empty()) {
                    msg.from.release();
                    continue;
                }

                // Phase 6 testing: simulated inbound packet loss. Apply
                // at the *datagram* level — before fragment reassembly and
                // before reliable-channel dedup — so that redundancy
                // mechanisms see realistic per-fragment / per-copy drop
                // patterns (a snapshot needs every fragment to assemble;
                // a reliable event survives if any of its 3 redundant
                // copies make it).
                if (shouldDropPacketLocked()) {
                    msg.from.release();
                    continue;
                }

                const auto channel = static_cast<net::ChannelId>(msg.header.channel);

                if (channel == net::ChannelId::Unreliable) {
                    // Snapshot stream + INPUT/PING. Fragmented if the
                    // payload exceeded the MTU floor; reassemble. The
                    // delay (when the latency simulator is on) is applied
                    // *after* reassembly — fragments arrive at their real
                    // wire times, and the *complete* assembled message
                    // takes the simulated trip back to the dispatch queue.
                    const bool isFragment = (msg.header.flags & 0x01) != 0;
                    if (!isFragment) {
                        recvUdpDelayed(std::move(msg.payload));
                    } else {
                        std::vector<uint8_t> assembled;
                        const auto r = unreliableReassembler_.addFragment(
                            msg.header, msg.payload.data(), static_cast<int>(msg.payload.size()), assembled);
                        if (r == net::FragmentReassembler::Result::Complete) {
                            recvUdpDelayed(std::move(assembled));
                        }
                    }
                } else if (channel == net::ChannelId::ReliableOrdered) {
                    // Phase 3d-5: dedup by sequence. The same event
                    // arrives k_reliableRedundancy times; only the first
                    // copy gets dispatched. Sliding-window bitmask
                    // handles wrap and out-of-order without false
                    // positives on the typical RTT × redundancy span.
                    if (acceptReliableSequence(msg.header.sequence))
                        recvUdpDelayed(std::move(msg.payload));
                }
                // Other channels: not yet defined; silently drop.
                msg.from.release();
            }
        }

        SDL_Delay(1);
    }
}

bool Client::applySnapshot(std::uint32_t snapshotTick, const std::uint8_t* bytes, Uint32 size, Uint32 wireSize)
{
    if (!snapshotApplyFn_)
        return false;

    std::uint32_t ackedTick = 0;
    const Uint64 applyNs = SDL_GetTicksNS();
    if (!snapshotApplyFn_(snapshotTick, bytes, size, applyNs, ackedTick))
        return false;

    if (ackedTick != 0)
        serverAckedClientTick_ = ackedTick;
    snapshotAppliedFlag_ = true;

    prevSnapshotApplyNs_ = lastSnapshotApplyNs_;
    lastSnapshotApplyNs_ = applyNs;

    if (prevSnapshotApplyNs_ != 0 && lastSnapshotApplyNs_ > prevSnapshotApplyNs_) {
        const Uint64 interval = lastSnapshotApplyNs_ - prevSnapshotApplyNs_;
        snapshotIntervalEmaNs_ = (3 * snapshotIntervalEmaNs_ + interval) / 4;
    }

    stats.registryUpdateSize = wireSize;
    ++registryUpdatesWindow;
    return true;
}

void Client::dispatchMessage(const uint8_t* data, Uint32 size)
{
    if (size < 1)
        return;
    auto type = static_cast<PacketType>(data[0]);
    const uint8_t* payload = data + 1;
    uint32_t payloadSize = size - 1;

    switch (type) {
    case PacketType::ASSIGN_CLIENT_ID: {
        // Phase 3d-1: ASSIGN_CLIENT_ID payload is now
        //   [entt::entity (4B)] [uint32_t connectionId (4B)]
        // Older 4-byte form (entity only) is still recognised so older
        // server builds keep working during the rollout.
        constexpr uint32_t k_oldSize = sizeof(entt::entity);
        constexpr uint32_t k_newSize = sizeof(entt::entity) + sizeof(uint32_t);
        constexpr uint32_t k_sessionSize = sizeof(entt::entity) + sizeof(std::uint64_t);
        if (payloadSize != k_oldSize && payloadSize != k_newSize && payloadSize != k_sessionSize) {
            SDL_Log("Client: received ASSIGN_CLIENT_ID packet of invalid size %u (expected %u, %u, or %u)",
                    payloadSize,
                    k_oldSize,
                    k_newSize,
                    k_sessionSize);
            return;
        }
        entt::entity assignedEntity;
        std::memcpy(&assignedEntity, payload, sizeof(entt::entity));
        localPlayerEntity = assignedEntity;
        if (payloadSize == k_newSize) {
            std::uint32_t connId = 0;
            std::memcpy(&connId, payload + sizeof(entt::entity), sizeof(uint32_t));
            connectionId_ = connId;
            SDL_Log("Client: assigned UDP connection id 0x%08x", connId);
        } else if (payloadSize == k_sessionSize) {
            std::uint64_t connId = 0;
            std::memcpy(&connId, payload + sizeof(entt::entity), sizeof(std::uint64_t));
            connectionId_ = connId;
            SDL_Log("Client: assigned UDP session id 0x%llx", static_cast<unsigned long long>(connId));
        }
        break;
    }
    case PacketType::UPDATE_REGISTRY: {
        // PR-10: wire format gained a tick prefix —
        //   payload = [tick:u32] [serializedBytes...]
        // The serialized bytes are what we both apply and stash as the
        // baseline for the next DELTA.
        if (payloadSize < sizeof(std::uint32_t))
            break;
        std::uint32_t snapshotTick = 0;
        std::memcpy(&snapshotTick, payload, sizeof(snapshotTick));
        const uint8_t* serBytes = payload + sizeof(snapshotTick);
        const Uint32 serSize = payloadSize - sizeof(snapshotTick);

        // PR-10 + PR-14: stash the raw serialized bytes as the
        // *keyframe* baseline.  Every DELTA in this keyframe window
        // will reference these same bytes; we do NOT update
        // `keyframePayload_` on DELTA arrival, so individual DELTA
        // drops don't cascade into baseline mismatches for the rest
        // of the window.  The next FULL replaces the keyframe.
        keyframePayload_.assign(serBytes, serBytes + serSize);
        keyframeTick_ = snapshotTick;
        applySnapshot(snapshotTick, serBytes, serSize, payloadSize);
        break;
    }
    case PacketType::UPDATE_REGISTRY_DELTA: {
        // PR-10: wire format —
        //   [tick:u32] [fromTick:u32] [baselineSize:u32] [rlePatch...]
        // We reconstruct the full snapshot bytes by applying the patch
        // on top of `lastSnapshotPayload_`, then run the existing
        // Loader::apply on the reconstructed bytes — exactly as if a
        // FULL had arrived.
        constexpr Uint32 k_headerSize = 3 * sizeof(std::uint32_t);
        if (payloadSize < k_headerSize)
            break;
        std::uint32_t snapshotTick = 0;
        std::uint32_t fromTick = 0;
        std::uint32_t baselineSize = 0;
        std::memcpy(&snapshotTick, payload + 0 * sizeof(std::uint32_t), sizeof(std::uint32_t));
        std::memcpy(&fromTick, payload + 1 * sizeof(std::uint32_t), sizeof(std::uint32_t));
        std::memcpy(&baselineSize, payload + 2 * sizeof(std::uint32_t), sizeof(std::uint32_t));

        // PR-14: every delta references the most recent KEYFRAME, not
        // the immediately previous snapshot.  Drop if we haven't
        // received the matching keyframe yet (e.g. it was lost on the
        // wire or we just connected and haven't seen one).  The next
        // periodic keyframe (every 8 snapshots ≈ 62 ms at 128 Hz)
        // re-syncs us — much faster than the pre-PR-14 cascade where
        // a single drop could blank out an entire keyframe window.
        if (fromTick != keyframeTick_ || keyframePayload_.empty() || keyframePayload_.size() != baselineSize) {
            // Silent drop is fine — receiver is allowed to discard
            // packets it can't apply. SDL_Log here would spam at
            // delta cadence on packet loss.
            break;
        }

        const uint8_t* patchData = payload + k_headerSize;
        const Uint32 patchSize = payloadSize - k_headerSize;
        auto reconstructed = registry_serialization::applyDelta(keyframePayload_, patchData, patchSize, baselineSize);
        if (reconstructed.empty())
            break; // malformed patch — drop, wait for next FULL.

        applySnapshot(snapshotTick, reconstructed.data(), static_cast<Uint32>(reconstructed.size()), payloadSize);

        // PR-14: do NOT replace the keyframe.  All deltas in this
        // keyframe window reference the same baseline; if we copied
        // the reconstructed bytes here we'd be back to pre-PR-14
        // cascade-on-loss.  The reconstructed bytes were already fed
        // into the Loader above; the keyframe stays at the FULL we
        // last saw.
        (void)reconstructed; // intentional: reconstructed buffer goes out of scope

        break;
    }
    case PacketType::PARTICLE_SPAWN: {
        if (payloadSize < sizeof(uint32_t))
            break;
        uint32_t count = 0;
        std::memcpy(&count, payload, sizeof(uint32_t));
        const uint8_t* eventData = payload + sizeof(uint32_t);
        constexpr std::uint32_t k_maxParticleEvents = 4096;
        if (count > k_maxParticleEvents)
            break;
        const std::size_t expectedSize = sizeof(uint32_t) + static_cast<std::size_t>(count) * sizeof(NetParticleEvent);
        if (static_cast<std::size_t>(payloadSize) != expectedSize)
            break;

        if (rawParticleEventFn_) {
            for (uint32_t i = 0; i < count; ++i) {
                NetParticleEvent evt;
                std::memcpy(&evt, eventData + i * sizeof(NetParticleEvent), sizeof(NetParticleEvent));
                rawParticleEventFn_(evt);
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
        latestMatchState_ = matchState;
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
        constexpr std::uint32_t k_maxKillEvents = 4096;
        if (count > k_maxKillEvents)
            break;
        const std::size_t expectedSize = sizeof(uint32_t) + static_cast<std::size_t>(count) * sizeof(NetKillEvent);
        if (static_cast<std::size_t>(payloadSize) != expectedSize)
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
    case PacketType::SHOT_DEBUG_REPORT: {
        // PR-20: parse the wire format defined in
        // `network/ShotDebugReport.hpp` back into a runtime
        // ShotDebugCapture.  Strict size checks at every step —
        // malformed packets get dropped silently rather than
        // crashing the diagnostic UI.
        using namespace net::shotdebug;
        if (payloadSize < sizeof(ReportHeader))
            break;
        ReportHeader rh{};
        std::memcpy(&rh, payload, sizeof(ReportHeader));

        ShotDebugCapture cap;
        cap.shooterClientId = 0; // server doesn't echo this back; not needed by UI
        cap.shotInputTick = rh.shotInputTick;
        cap.hitTargetClientId = rh.hitTargetClientId;
        cap.hitRegion = rh.hitRegion;
        cap.origin = {rh.originX, rh.originY, rh.originZ};
        cap.direction = {rh.dirX, rh.dirY, rh.dirZ};
        cap.range = rh.range;
        cap.hitPoint = {rh.hitX, rh.hitY, rh.hitZ};
        cap.targets.reserve(rh.numTargets);

        std::size_t off = sizeof(ReportHeader);
        bool malformed = false;
        for (std::uint8_t t = 0; t < rh.numTargets; ++t) {
            if (off + sizeof(TargetHeader) > payloadSize) {
                malformed = true;
                break;
            }
            TargetHeader th{};
            std::memcpy(&th, payload + off, sizeof(TargetHeader));
            off += sizeof(TargetHeader);
            const std::size_t need = std::size_t{th.numCapsules} * sizeof(WireCapsule);
            if (off + need > payloadSize) {
                malformed = true;
                break;
            }
            ShotDebugCapture::Target tgt;
            tgt.clientId = th.targetClientId;
            tgt.capsules.reserve(th.numCapsules);
            for (std::uint8_t c = 0; c < th.numCapsules; ++c) {
                WireCapsule wc{};
                std::memcpy(&wc, payload + off, sizeof(WireCapsule));
                off += sizeof(WireCapsule);
                WorldCapsule rc{};
                rc.pointA = {wc.pointAx, wc.pointAy, wc.pointAz};
                rc.pointB = {wc.pointBx, wc.pointBy, wc.pointBz};
                rc.radius = wc.radius;
                rc.region = static_cast<BodyRegion>(wc.region);
                tgt.capsules.push_back(rc);
            }
            cap.targets.push_back(std::move(tgt));
        }
        if (!malformed && shotDebugFn_)
            shotDebugFn_(cap);
        break;
    }
    case PacketType::LOBBY_UPDATE: {
        if (payloadSize < sizeof(LobbyUpdateEvent))
            break;

        LobbyUpdateEvent lu{};
        std::memcpy(&lu, payload, sizeof(LobbyUpdateEvent));

        if (latestLobbyPlayers_) {
            switch (lu.type) {
            case LobbyUpdateEvent::Type::PlayerJoined:
                if (std::none_of(latestLobbyPlayers_->begin(),
                                 latestLobbyPlayers_->end(),
                                 [id = lu.id](const LobbyPlayer& p) { return p.id == id; }))
                {
                    latestLobbyPlayers_->push_back(LobbyPlayer{lu.id});
                }
                break;
            case LobbyUpdateEvent::Type::PlayerLeft:
                latestLobbyPlayers_->erase(std::remove_if(latestLobbyPlayers_->begin(),
                                                          latestLobbyPlayers_->end(),
                                                          [id = lu.id](const LobbyPlayer& p) { return p.id == id; }),
                                           latestLobbyPlayers_->end());
                break;
            case LobbyUpdateEvent::Type::PlayerReady:
            case LobbyUpdateEvent::Type::PlayerUnready:
                for (auto& player : *latestLobbyPlayers_) {
                    if (player.id == lu.id) {
                        player.ready = lu.type == LobbyUpdateEvent::Type::PlayerReady;
                        break;
                    }
                }
                break;
            case LobbyUpdateEvent::Type::PlayerNewHost:
                for (auto& player : *latestLobbyPlayers_)
                    player.isHost = player.id == lu.id;
                break;
            }
        }

        if (lobbyUpdateFn_)
            lobbyUpdateFn_(lu);
        break;
    }
    case PacketType::LOBBY_STATE: {
        if (payloadSize < sizeof(int) + sizeof(uint32_t))
            break;

        ClientId localId{};
        std::memcpy(&localId.value, payload, sizeof(int));

        uint32_t count = 0;
        std::memcpy(&count, payload + sizeof(int), sizeof(uint32_t));

        constexpr std::uint32_t k_maxLobbyPlayers = 256;
        if (count > k_maxLobbyPlayers)
            break;
        const size_t expectedSize =
            sizeof(int) + sizeof(uint32_t) + static_cast<std::size_t>(count) * sizeof(LobbyPlayer);
        if (static_cast<std::size_t>(payloadSize) != expectedSize)
            break;

        std::vector<LobbyPlayer> players(count);
        std::memcpy(players.data(), payload + sizeof(int) + sizeof(uint32_t), count * sizeof(LobbyPlayer));

        latestLobbyPlayers_ = players;
        latestLobbyLocalId_ = localId;

        if (lobbyStateFn_)
            lobbyStateFn_(players, localId);
        break;
    }
    case PacketType::TEXT_CHAT: {
        const auto chat = net::chat::decodeServerText(std::span<const std::uint8_t>(data, size));
        if (chat && textChatFn_)
            textChatFn_(*chat);
        break;
    }
    case PacketType::VOICE_FRAME: {
        const auto frame = net::voice::decodeServerFrame(std::span<const std::uint8_t>(data, size));
        if (frame && voiceFrameFn_)
            voiceFrameFn_(*frame);
        break;
    }
    default:
        SDL_Log("Client: unknown message type %d", static_cast<int>(type));
        break;
    }
}

bool Client::poll()
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
    //
    // Phase 3d: also drain UDP-received payloads queued by the network
    // thread. UDP messages already arrive as discrete payloads (one per
    // datagram) so they go straight in alongside the TCP-drained frames.
    // Both transports use the same `[PacketType][rest]` payload format,
    // so dispatchMessage handles them identically.
    std::vector<std::vector<uint8_t>> ready;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!msgStream.drainComplete([&](const void* data, Uint32 size) {
                ready.emplace_back(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
            }))
        {
            socketDead_.store(true, std::memory_order_relaxed);
            return false;
        }
        if (!udpRecvQueue_.empty()) {
            for (auto& msg : udpRecvQueue_) {
                ready.emplace_back(std::move(msg));
            }
            udpRecvQueue_.clear();
        }
    }

    // Dispatch (lock-free).  Stats are touched here only — they're not
    // cross-thread shared (only this game thread reads/writes them).
    for (const auto& msg : ready) {
        stats.bytesRecvTotal += msg.size() + 4; // +4 for the length prefix that drainComplete already stripped
        bytesRecvWindow += msg.size() + 4;
        dispatchMessage(msg.data(), static_cast<Uint32>(msg.size()));
    }

    return true;
}
