/// @file UdpSessionTransport.cpp
/// @brief UDP-first session transport implementation.

#include "UdpSessionTransport.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace net
{
namespace
{
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    const std::size_t off = out.size();
    out.resize(off + sizeof(std::uint32_t));
    writeU32Le(out.data() + off, value);
}

void appendU64(std::vector<std::uint8_t>& out, std::uint64_t value)
{
    const std::size_t off = out.size();
    out.resize(off + sizeof(std::uint64_t));
    writeU64Le(out.data() + off, value);
}

std::uint32_t randomU32(std::mt19937_64& rng)
{
    std::uniform_int_distribution<std::uint32_t> dist(1, std::numeric_limits<std::uint32_t>::max());
    return dist(rng);
}

std::uint64_t randomU64(std::mt19937_64& rng)
{
    std::uniform_int_distribution<std::uint64_t> dist(1, std::numeric_limits<std::uint64_t>::max());
    return dist(rng);
}
} // namespace

UdpSessionTransport::UdpSessionTransport()
{
    rng_.seed(std::random_device{}());
}

std::size_t UdpSessionTransport::channelIndex(ChannelId channel) noexcept
{
    const auto idx = static_cast<std::size_t>(channel);
    return idx < static_cast<std::size_t>(ChannelId::Count) ? idx : 0;
}

bool UdpSessionTransport::isReliable(ChannelId channel) noexcept
{
    return channel == ChannelId::ControlReliableOrdered || channel == ChannelId::EventReliableOrdered ||
           channel == ChannelId::ReliableOrdered || channel == ChannelId::ReliableUnordered;
}

bool UdpSessionTransport::seqMoreRecent(std::uint32_t s1, std::uint32_t s2) noexcept
{
    constexpr std::uint32_t k_half = 0x80000000u;
    return ((s1 > s2) && (s1 - s2 <= k_half)) || ((s2 > s1) && (s2 - s1 > k_half));
}

bool UdpSessionTransport::seqAcked(std::uint32_t seq, std::uint32_t ack, std::uint32_t ackBits) noexcept
{
    if (seq == ack)
        return true;
    if (!seqMoreRecent(ack, seq))
        return false;
    const std::uint32_t diff = ack - seq;
    return diff >= 1 && diff <= 32 && ((ackBits & (1u << (diff - 1))) != 0);
}

bool UdpSessionTransport::resolveAddress(const char* host, Uint16 port, UdpEndpointAddr& out, int timeoutMs)
{
    NET_Address* addr = NET_ResolveHostname(host);
    if (!addr)
        return false;
    const NET_Status status = NET_WaitUntilResolved(addr, timeoutMs);
    if (status != NET_SUCCESS) {
        NET_UnrefAddress(addr);
        return false;
    }
    out.release();
    out.addr = addr;
    out.port = port;
    return true;
}

bool UdpSessionTransport::openServer(const char* bindAddr, Uint16 port)
{
    close();
    mode_ = Mode::Server;
    clientNonce_ = 0;
    clientConnectionId_ = 0;
    if (!endpoint_.open(bindAddr, port))
        return false;
    return true;
}

bool UdpSessionTransport::connectClient(const char* host, Uint16 port, int timeoutMs)
{
    close();
    mode_ = Mode::Client;
    clientNonce_ = randomU32(rng_);
    clientConnectionId_ = 0;

    if (!endpoint_.open(nullptr, 0))
        return false;
    if (!resolveAddress(host, port, serverAddr_, timeoutMs))
        return false;
    if (relayConfig_.enabled && !relayConfig_.host.empty() && relayConfig_.port != 0)
        resolveAddress(relayConfig_.host.c_str(), relayConfig_.port, relayAddr_, timeoutMs);

    const Uint64 started = SDL_GetTicks();
    lastConnectAttemptMs_ = 0;
    while (timeoutMs < 0 || SDL_GetTicks() - started <= static_cast<Uint64>(timeoutMs)) {
        pump();
        Event event;
        while (pollEvent(event)) {
            if (event.type == EventType::Connected)
                return true;
        }

        const Uint64 now = SDL_GetTicks();
        if (lastConnectAttemptMs_ == 0 || now - lastConnectAttemptMs_ >= 100) {
            sendConnectionRequest(false);
            if (relayConfig_.enabled)
                sendConnectionRequest(true);
            lastConnectAttemptMs_ = now;
        }
        SDL_Delay(5);
    }

    return false;
}

void UdpSessionTransport::close()
{
    peers_.clear();
    connectionByClientNonce_.clear();
    events_.clear();
    endpoint_.close();
    serverAddr_.release();
    relayAddr_.release();
    clientConnectionId_ = 0;
    stats_ = Stats{};
}

void UdpSessionTransport::setRelayConfig(const RelayConfig& cfg)
{
    relayConfig_ = cfg;
    relayAddr_.release();
    if (cfg.enabled && !cfg.host.empty() && cfg.port != 0) {
        resolveAddress(cfg.host.c_str(), cfg.port, relayAddr_, 1000);
    }
}

void UdpSessionTransport::queueEvent(Event&& event)
{
    events_.push_back(std::move(event));
}

bool UdpSessionTransport::pollEvent(Event& out)
{
    pump();
    if (events_.empty())
        return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

void UdpSessionTransport::pump()
{
    if (!endpoint_.isOpen())
        return;

    UdpReceivedMessage msg;
    int drained = 0;
    while (drained < 512 && endpoint_.tryReceive(msg)) {
        ++drained;
        stats_.bytesRecv += sizeof(PacketHeader) + msg.payload.size();
        ++stats_.packetsRecv;

        if (msg.header.kind == static_cast<std::uint8_t>(PacketKind::RelayPayload)) {
            processRelayPayload(msg);
        } else if (msg.header.kind == static_cast<std::uint8_t>(PacketKind::DirectoryControl)) {
            queueEvent(Event{.type = EventType::DirectoryControl, .payload = std::move(msg.payload)});
        } else {
            processDatagram(msg, false);
        }
        msg.from.release();
    }

    const Uint64 now = SDL_GetTicks();
    retransmitReliable(now);
    sendKeepAlives(now);
    dropTimedOutPeers(now);
}

UdpSessionTransport::Peer* UdpSessionTransport::findPeer(std::uint64_t connectionId)
{
    auto it = peers_.find(connectionId);
    return it == peers_.end() ? nullptr : &it->second;
}

UdpSessionTransport::Peer&
UdpSessionTransport::createServerPeer(const UdpEndpointAddr& from, std::uint32_t clientNonce, bool viaRelay)
{
    if (auto existing = connectionByClientNonce_.find(clientNonce); existing != connectionByClientNonce_.end()) {
        return peers_.at(existing->second);
    }

    const std::uint64_t connectionId = randomU64(rng_);
    auto [it, _] = peers_.try_emplace(connectionId);
    Peer& peer = it->second;
    peer.connectionId = connectionId;
    peer.lastHeardMs = SDL_GetTicks();
    peer.useRelay = viaRelay;
    peer.relayClientNonce = clientNonce;
    peer.relayServerId = relayConfig_.serverId;
    if (viaRelay) {
        peer.relayAddr = from;
        peer.hasRelay = true;
        peer.lastRelayHeardMs = peer.lastHeardMs;
    } else {
        peer.directAddr = from;
        peer.hasDirect = true;
        peer.lastDirectHeardMs = peer.lastHeardMs;
    }
    connectionByClientNonce_[clientNonce] = connectionId;
    return peer;
}

void UdpSessionTransport::sendConnectionRequest(bool viaRelay)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(12);
    appendU32(payload, clientNonce_);
    appendU64(payload, 0);

    PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(PacketKind::ConnectionRequest);
    hdr.channel = static_cast<std::uint8_t>(ChannelId::ControlReliableOrdered);
    hdr.sequence = 0;
    hdr.routeId = viaRelay ? 1 : 0;

    if (viaRelay && relayConfig_.enabled && relayAddr_.addr) {
        Peer temp;
        temp.connectionId = 0;
        temp.relayAddr = relayAddr_;
        temp.hasRelay = true;
        temp.useRelay = true;
        temp.relayServerId = relayConfig_.serverId;
        temp.relayClientNonce = clientNonce_;
        sendViaRelay(temp, hdr, payload.data(), static_cast<int>(payload.size()));
    } else if (serverAddr_.addr) {
        sendDirect(serverAddr_, hdr, payload.data(), static_cast<int>(payload.size()), 1);
    }
}

void UdpSessionTransport::sendConnectionAccepted(Peer& peer)
{
    std::vector<std::uint8_t> payload;
    appendU64(payload, peer.connectionId);

    PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(PacketKind::ConnectionAccepted);
    hdr.connectionId = peer.connectionId;
    hdr.channel = static_cast<std::uint8_t>(ChannelId::ControlReliableOrdered);
    hdr.routeId = peer.useRelay ? 1 : 0;
    sendPacket(peer, hdr, payload.data(), static_cast<int>(payload.size()), 3);
}

bool UdpSessionTransport::send(
    std::uint64_t connectionId, ChannelId channel, const void* payload, int payloadLen, int redundancy)
{
    Peer* peer = findPeer(connectionId);
    if (!peer || payloadLen < 0)
        return false;

    const std::size_t chIdx = channelIndex(channel);
    ChannelState& ch = peer->channels[chIdx];

    if (isReliable(channel)) {
        if (ch.pending.size() >= k_maxReliablePending)
            return false;
        PendingReliable pending;
        pending.sequence = ch.nextSequence++;
        pending.channel = channel;
        pending.payload.assign(static_cast<const std::uint8_t*>(payload),
                               static_cast<const std::uint8_t*>(payload) + payloadLen);
        ch.pending.push_back(std::move(pending));
        stats_.reliablePending =
            std::max<std::uint32_t>(stats_.reliablePending, static_cast<std::uint32_t>(ch.pending.size()));
        PendingReliable& back = ch.pending.back();
        PacketHeader hdr{};
        hdr.kind = static_cast<std::uint8_t>(PacketKind::Payload);
        hdr.connectionId = connectionId;
        hdr.sequence = back.sequence;
        hdr.channel = static_cast<std::uint8_t>(channel);
        hdr.routeId = peer->useRelay ? 1 : 0;
        const bool ok = sendPacket(*peer, hdr, back.payload.data(), static_cast<int>(back.payload.size()), 1);
        back.lastSendMs = SDL_GetTicks();
        back.attempts = 1;
        return ok;
    }

    PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(PacketKind::Payload);
    hdr.connectionId = connectionId;
    hdr.sequence = ch.nextSequence++;
    hdr.channel = static_cast<std::uint8_t>(channel);
    hdr.routeId = peer->useRelay ? 1 : 0;
    return sendPacket(*peer, hdr, payload, payloadLen, redundancy);
}

bool UdpSessionTransport::disconnect(std::uint64_t connectionId)
{
    Peer* peer = findPeer(connectionId);
    if (!peer)
        return false;
    PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(PacketKind::Disconnect);
    hdr.connectionId = connectionId;
    hdr.channel = static_cast<std::uint8_t>(ChannelId::ControlReliableOrdered);
    const bool ok = sendPacket(*peer, hdr, nullptr, 0, 3);
    peers_.erase(connectionId);
    return ok;
}

bool UdpSessionTransport::sendDirectoryControl(const UdpEndpointAddr& dest, const void* payload, int payloadLen)
{
    PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(PacketKind::DirectoryControl);
    hdr.channel = static_cast<std::uint8_t>(ChannelId::ControlReliableOrdered);
    return sendDirect(dest, hdr, payload, payloadLen, 1);
}

bool UdpSessionTransport::sendPacket(Peer& peer, PacketHeader hdr, const void* payload, int payloadLen, int redundancy)
{
    const auto channel = static_cast<ChannelId>(hdr.channel);
    ChannelState& ch = peer.channels[channelIndex(channel)];
    hdr.ack = ch.recvHighest;
    hdr.ackBits = ch.recvAckBits;

    const Uint64 now = SDL_GetTicks();
    if (!preferRelay_ && peer.useRelay && peer.hasDirect && peer.lastDirectHeardMs != 0 &&
        now - peer.lastDirectHeardMs <= k_routeUnhealthyMs)
    {
        peer.useRelay = false;
    } else if (!peer.useRelay && peer.hasRelay && peer.lastDirectHeardMs != 0 &&
               now - peer.lastDirectHeardMs > k_routeUnhealthyMs)
    {
        peer.useRelay = true;
    }

    hdr.routeId = peer.useRelay ? 1 : 0;

    if (peer.useRelay && peer.hasRelay)
        return sendViaRelay(peer, hdr, payload, payloadLen);
    if (peer.hasDirect)
        return sendDirect(peer.directAddr, hdr, payload, payloadLen, redundancy);
    if (peer.hasRelay)
        return sendViaRelay(peer, hdr, payload, payloadLen);
    return false;
}

bool UdpSessionTransport::sendDirect(
    const UdpEndpointAddr& dest, PacketHeader hdr, const void* payload, int payloadLen, int redundancy)
{
    bool ok = true;
    if (payloadLen > k_maxPayloadBytes) {
        ok = endpoint_.sendFragmented(dest, hdr, payload, payloadLen, redundancy);
    } else {
        for (int i = 0; i < std::max(1, redundancy); ++i)
            ok = endpoint_.send(dest, hdr, payload, payloadLen) && ok;
    }
    if (ok) {
        stats_.bytesSent += sizeof(PacketHeader) + static_cast<std::size_t>(std::max(0, payloadLen));
        ++stats_.packetsSent;
    }
    return ok;
}

bool UdpSessionTransport::sendViaRelay(Peer& peer, PacketHeader hdr, const void* payload, int payloadLen)
{
    if (!peer.relayAddr.addr)
        return false;

    if (payloadLen < 0)
        return false;

    constexpr int k_relayEnvelopeBytes =
        static_cast<int>(sizeof(std::uint32_t) * 2 + sizeof(std::uint64_t) + sizeof(std::uint16_t));
    constexpr int k_maxRelayInnerPayload = k_maxPacketBytes - static_cast<int>(sizeof(PacketHeader)) -
                                           k_relayEnvelopeBytes - static_cast<int>(sizeof(PacketHeader));
    static_assert(k_maxRelayInnerPayload > 0, "relay MTU budget must leave room for inner payload bytes");

    const auto* bytes = static_cast<const std::uint8_t*>(payload);
    auto sendInner = [&](PacketHeader innerHdr, const std::uint8_t* innerPayload, int innerPayloadLen) {
        const std::vector<std::uint8_t> inner = makeDatagram(innerHdr, innerPayload, innerPayloadLen);
        std::vector<std::uint8_t> envelope;
        envelope.reserve(k_relayEnvelopeBytes + inner.size());
        appendU32(envelope, peer.relayServerId ? peer.relayServerId : relayConfig_.serverId);
        appendU32(envelope, peer.relayClientNonce ? peer.relayClientNonce : relayConfig_.clientNonce);
        appendU64(envelope, relayConfig_.relayToken);
        const std::size_t lenOff = envelope.size();
        envelope.resize(envelope.size() + sizeof(std::uint16_t));
        writeU16Le(envelope.data() + lenOff, static_cast<std::uint16_t>(inner.size()));
        envelope.insert(envelope.end(), inner.begin(), inner.end());

        PacketHeader outer{};
        outer.kind = static_cast<std::uint8_t>(PacketKind::RelayPayload);
        outer.connectionId = innerHdr.connectionId;
        outer.channel = innerHdr.channel;
        outer.routeId = 1;
        const bool sent = endpoint_.send(peer.relayAddr, outer, envelope.data(), static_cast<int>(envelope.size()));
        if (sent) {
            stats_.bytesSent += sizeof(PacketHeader) + envelope.size();
            ++stats_.packetsSent;
        }
        return sent;
    };

    bool ok = true;
    if (payloadLen <= k_maxRelayInnerPayload) {
        hdr.flags = static_cast<std::uint8_t>(hdr.flags & ~k_flagFragmented);
        hdr.fragmentInfo = 0;
        ok = sendInner(hdr, bytes, payloadLen);
    } else {
        const int fragCount = (payloadLen + k_maxRelayInnerPayload - 1) / k_maxRelayInnerPayload;
        if (fragCount > 255)
            return false;
        for (int i = 0; i < fragCount; ++i) {
            const int offset = i * k_maxRelayInnerPayload;
            const int chunkLen = std::min(k_maxRelayInnerPayload, payloadLen - offset);
            PacketHeader fragHdr = hdr;
            fragHdr.flags = static_cast<std::uint8_t>(fragHdr.flags | k_flagFragmented);
            fragHdr.fragmentInfo = static_cast<std::uint16_t>((i << 8) | fragCount);
            ok = sendInner(fragHdr, bytes ? bytes + offset : nullptr, chunkLen) && ok;
        }
    }

    if (ok)
        stats_.relayActive = true;
    return ok;
}

void UdpSessionTransport::processRelayPayload(UdpReceivedMessage& msg)
{
    constexpr std::size_t k_relayEnvelopeBytes =
        sizeof(std::uint32_t) * 2 + sizeof(std::uint64_t) + sizeof(std::uint16_t);
    if (msg.payload.size() < k_relayEnvelopeBytes)
        return;
    const std::uint8_t* data = msg.payload.data();
    const std::uint16_t innerLen = readU16Le(data + 16);
    if (msg.payload.size() < k_relayEnvelopeBytes + innerLen || innerLen < sizeof(PacketHeader))
        return;

    PacketHeader innerHdr{};
    if (!decodePacketHeader(data + k_relayEnvelopeBytes, innerLen, innerHdr))
        return;

    UdpReceivedMessage inner;
    inner.from = msg.from;
    inner.header = innerHdr;
    const std::uint8_t* payload = data + k_relayEnvelopeBytes + sizeof(PacketHeader);
    const std::size_t payloadLen = innerLen - sizeof(PacketHeader);
    inner.payload.assign(payload, payload + payloadLen);
    processDatagram(inner, true);
}

void UdpSessionTransport::processDatagram(UdpReceivedMessage& msg, bool viaRelay)
{
    const auto kind = static_cast<PacketKind>(msg.header.kind);
    if (kind == PacketKind::ConnectionRequest && mode_ == Mode::Server) {
        if (msg.payload.size() < sizeof(std::uint32_t))
            return;
        const std::uint32_t nonce = readU32Le(msg.payload.data());
        Peer& peer = createServerPeer(msg.from, nonce, viaRelay);
        peer.lastHeardMs = SDL_GetTicks();
        if (viaRelay) {
            peer.relayAddr = msg.from;
            peer.hasRelay = true;
            peer.useRelay = true;
            peer.lastRelayHeardMs = peer.lastHeardMs;
        } else {
            peer.directAddr = msg.from;
            peer.hasDirect = true;
            peer.lastDirectHeardMs = peer.lastHeardMs;
            if (!preferRelay_)
                peer.useRelay = false;
        }
        sendConnectionAccepted(peer);
        if (!peer.connectedEventSent) {
            queueEvent(Event{.type = EventType::Connected, .connectionId = peer.connectionId, .viaRelay = viaRelay});
            peer.connectedEventSent = true;
        }
        return;
    }

    if (kind == PacketKind::ConnectionAccepted && mode_ == Mode::Client) {
        std::uint64_t connId = msg.header.connectionId;
        if (msg.payload.size() >= sizeof(std::uint64_t))
            connId = readU64Le(msg.payload.data());
        if (connId == 0)
            return;

        auto [it, inserted] = peers_.try_emplace(connId);
        Peer& peer = it->second;
        peer.connectionId = connId;
        peer.lastHeardMs = SDL_GetTicks();
        if (viaRelay) {
            peer.relayAddr = msg.from;
            peer.hasRelay = true;
            peer.useRelay = true;
            peer.relayServerId = relayConfig_.serverId;
            peer.relayClientNonce = clientNonce_;
            peer.lastRelayHeardMs = peer.lastHeardMs;
        } else {
            peer.directAddr = msg.from;
            peer.hasDirect = true;
            peer.lastDirectHeardMs = peer.lastHeardMs;
            peer.useRelay = preferRelay_;
        }
        clientConnectionId_ = connId;
        if (inserted)
            queueEvent(Event{.type = EventType::Connected, .connectionId = connId, .viaRelay = viaRelay});
        return;
    }

    Peer* peer = findPeer(msg.header.connectionId);
    if (!peer)
        return;

    peer->lastHeardMs = SDL_GetTicks();
    if (viaRelay) {
        peer->relayAddr = msg.from;
        peer->hasRelay = true;
        peer->useRelay = true;
        peer->lastRelayHeardMs = peer->lastHeardMs;
    } else {
        peer->directAddr = msg.from;
        peer->hasDirect = true;
        peer->lastDirectHeardMs = peer->lastHeardMs;
        if (!preferRelay_)
            peer->useRelay = false;
    }

    if (kind == PacketKind::Disconnect) {
        queueEvent(Event{.type = EventType::Disconnected, .connectionId = peer->connectionId, .viaRelay = viaRelay});
        peers_.erase(peer->connectionId);
        return;
    }

    if (kind == PacketKind::KeepAlive) {
        processAcks(*peer, msg.header);
        return;
    }

    if (kind != PacketKind::Payload)
        return;

    std::vector<std::uint8_t> payload;
    if ((msg.header.flags & k_flagFragmented) != 0) {
        ChannelState& ch = peer->channels[channelIndex(static_cast<ChannelId>(msg.header.channel))];
        const auto result =
            ch.reassembler.addFragment(msg.header, msg.payload.data(), static_cast<int>(msg.payload.size()), payload);
        if (result != FragmentReassembler::Result::Complete)
            return;
    } else {
        payload = std::move(msg.payload);
    }

    processPayload(*peer, msg.header, std::move(payload), viaRelay);
}

void UdpSessionTransport::processPayload(Peer& peer,
                                         PacketHeader hdr,
                                         std::vector<std::uint8_t>&& payload,
                                         bool viaRelay)
{
    processAcks(peer, hdr);

    const auto channel = static_cast<ChannelId>(hdr.channel);
    ChannelState& ch = peer.channels[channelIndex(channel)];
    rememberReceived(ch, hdr.sequence);

    if (isReliable(channel)) {
        deliverReliable(peer, channel, hdr.sequence, std::move(payload), viaRelay);
        return;
    }

    if (channel == ChannelId::SnapshotUnreliableSequenced && !acceptUnreliableSequenced(ch, hdr.sequence))
        return;

    queueEvent(Event{.type = EventType::Payload,
                     .connectionId = peer.connectionId,
                     .channel = channel,
                     .payload = std::move(payload),
                     .viaRelay = viaRelay});
}

void UdpSessionTransport::processAcks(Peer& peer, const PacketHeader& hdr)
{
    ChannelState& ch = peer.channels[channelIndex(static_cast<ChannelId>(hdr.channel))];
    if (ch.pending.empty())
        return;
    const Uint64 now = SDL_GetTicks();
    for (auto it = ch.pending.begin(); it != ch.pending.end();) {
        if (seqAcked(it->sequence, hdr.ack, hdr.ackBits)) {
            if (it->lastSendMs != 0) {
                const float sample = static_cast<float>(now - it->lastSendMs);
                peer.rttMs = peer.rttMs <= 0.0f ? sample : peer.rttMs * 0.8f + sample * 0.2f;
                stats_.rttMs = peer.rttMs;
            }
            it = ch.pending.erase(it);
        } else {
            ++it;
        }
    }
}

void UdpSessionTransport::rememberReceived(ChannelState& state, std::uint32_t sequence)
{
    if (!state.recvAny) {
        state.recvAny = true;
        state.recvHighest = sequence;
        state.recvAckBits = 0;
        return;
    }
    if (sequence == state.recvHighest)
        return;
    if (seqMoreRecent(sequence, state.recvHighest)) {
        const std::uint32_t shift = sequence - state.recvHighest;
        state.recvAckBits = shift >= 32 ? 0 : ((state.recvAckBits << shift) | (1u << (shift - 1)));
        state.recvHighest = sequence;
        return;
    }
    const std::uint32_t diff = state.recvHighest - sequence;
    if (diff >= 1 && diff <= 32)
        state.recvAckBits |= 1u << (diff - 1);
}

bool UdpSessionTransport::acceptUnreliableSequenced(ChannelState& state, std::uint32_t sequence)
{
    if (!state.orderedAny) {
        state.orderedAny = true;
        state.orderedNext = sequence;
        return true;
    }
    if (seqMoreRecent(sequence, state.orderedNext)) {
        state.orderedNext = sequence;
        return true;
    }
    return false;
}

void UdpSessionTransport::deliverReliable(
    Peer& peer, ChannelId channel, std::uint32_t sequence, std::vector<std::uint8_t>&& payload, bool viaRelay)
{
    ChannelState& ch = peer.channels[channelIndex(channel)];
    if (!ch.orderedAny) {
        ch.orderedAny = true;
        ch.orderedNext = sequence;
    }

    if (sequence == ch.orderedNext) {
        queueEvent(Event{.type = EventType::Payload,
                         .connectionId = peer.connectionId,
                         .channel = channel,
                         .payload = std::move(payload),
                         .viaRelay = viaRelay});
        ++ch.orderedNext;
        while (true) {
            auto it = ch.orderedBuffer.find(ch.orderedNext);
            if (it == ch.orderedBuffer.end())
                break;
            queueEvent(Event{.type = EventType::Payload,
                             .connectionId = peer.connectionId,
                             .channel = channel,
                             .payload = std::move(it->second),
                             .viaRelay = viaRelay});
            ch.orderedBuffer.erase(it);
            ++ch.orderedNext;
        }
        return;
    }

    if (seqMoreRecent(sequence, ch.orderedNext)) {
        ch.orderedBuffer.try_emplace(sequence, std::move(payload));
    }
}

void UdpSessionTransport::retransmitReliable(Uint64 nowMs)
{
    for (auto& [connId, peer] : peers_) {
        for (ChannelState& ch : peer.channels) {
            for (PendingReliable& pending : ch.pending) {
                const Uint64 interval = static_cast<Uint64>(std::max<float>(k_retransmitFloorMs, peer.rttMs * 1.5f));
                if (pending.lastSendMs != 0 && nowMs - pending.lastSendMs < interval)
                    continue;
                PacketHeader hdr{};
                hdr.kind = static_cast<std::uint8_t>(PacketKind::Payload);
                hdr.connectionId = connId;
                hdr.sequence = pending.sequence;
                hdr.channel = static_cast<std::uint8_t>(pending.channel);
                sendPacket(peer, hdr, pending.payload.data(), static_cast<int>(pending.payload.size()), 1);
                pending.lastSendMs = nowMs;
                ++pending.attempts;
                ++stats_.reliableRetransmits;
            }
        }
    }
}

void UdpSessionTransport::sendKeepAlives(Uint64 nowMs)
{
    for (auto& [connId, peer] : peers_) {
        if (nowMs - peer.lastKeepAliveMs < k_keepAliveMs)
            continue;
        peer.lastKeepAliveMs = nowMs;
        PacketHeader hdr{};
        hdr.kind = static_cast<std::uint8_t>(PacketKind::KeepAlive);
        hdr.connectionId = connId;
        hdr.channel = static_cast<std::uint8_t>(ChannelId::ControlReliableOrdered);
        sendPacket(peer, hdr, nullptr, 0, 1);

        if (mode_ == Mode::Client && peer.useRelay && serverAddr_.addr) {
            ChannelState& ch = peer.channels[channelIndex(ChannelId::ControlReliableOrdered)];
            hdr.ack = ch.recvHighest;
            hdr.ackBits = ch.recvAckBits;
            hdr.routeId = 0;
            sendDirect(serverAddr_, hdr, nullptr, 0, 1);
        }
    }
}

void UdpSessionTransport::dropTimedOutPeers(Uint64 nowMs)
{
    std::vector<std::uint64_t> dead;
    for (const auto& [connId, peer] : peers_) {
        if (peer.lastHeardMs != 0 && nowMs - peer.lastHeardMs > k_timeoutMs)
            dead.push_back(connId);
    }
    for (std::uint64_t connId : dead) {
        queueEvent(Event{.type = EventType::Disconnected, .connectionId = connId});
        peers_.erase(connId);
        if (clientConnectionId_ == connId)
            clientConnectionId_ = 0;
    }
}

} // namespace net
