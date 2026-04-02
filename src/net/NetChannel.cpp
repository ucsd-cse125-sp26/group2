#include "NetChannel.hpp"

#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
// Returns true if seq is "more recent" than reference, using half-range.
bool seqGreater(uint16_t seq, uint16_t ref)
{
    return ((seq > ref) && (seq - ref <= 0x8000u)) || ((seq < ref) && (ref - seq > 0x8000u));
}
} // namespace

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void NetChannel::init(UdpSocket* socket, const sockaddr_in* peerAddr)
{
    sock = socket;
    if (peerAddr) {
        peer = *peerAddr;
        hasPeer = true;
    }
}

// ---------------------------------------------------------------------------
// Send
// ---------------------------------------------------------------------------

int NetChannel::send(void* packet, int totalBytes, PacketType type, uint8_t clientId)
{
    if (!sock || !sock->isOpen() || !hasPeer)
        return -1;

    auto* hdr = static_cast<PacketHeader*>(packet);
    hdr->magic = k_magic;
    hdr->sequence = outSeq++;
    hdr->ack = inAck;
    hdr->ackBits = inBits;
    hdr->type = static_cast<uint8_t>(type);
    hdr->clientId = clientId;

    int sent = sock->sendTo(packet, totalBytes, peer);
    if (sent > 0)
        packetsSent++;
    return sent;
}

// ---------------------------------------------------------------------------
// Receive — reads one datagram, validates header, checks for duplicates
// ---------------------------------------------------------------------------

int NetChannel::recv(void* buf, int maxLen, sockaddr_in& outFrom)
{
    if (!sock || !sock->isOpen())
        return -1;

    int n = sock->recvFrom(buf, maxLen, outFrom);
    if (n <= 0)
        return n;
    if (n < static_cast<int>(sizeof(PacketHeader)))
        return 0; // too short, discard

    auto* hdr = static_cast<PacketHeader*>(buf);
    if (hdr->magic != k_magic)
        return 0; // bad magic

    uint16_t seq = hdr->sequence;

    // Duplicate / stale detection
    if (!checkAndUpdateSeq(seq)) {
        packetsDropped++;
        return 0;
    }

    // Update our local ack tracking (what we'll report back in outgoing pkts)
    if (seqGreater(seq, inAck) || packetsReceived == 0) {
        uint16_t delta = seq - inAck;
        inBits = static_cast<uint16_t>((inBits << delta) | (1u << (delta - 1)));
        inAck = seq;
    } else {
        uint16_t delta = inAck - seq;
        if (delta < 16)
            inBits |= static_cast<uint16_t>(1u << delta);
    }

    packetsReceived++;
    return n;
}

// ---------------------------------------------------------------------------
// Duplicate detection using a 64-bit sliding window bitmask
// ---------------------------------------------------------------------------

bool NetChannel::checkAndUpdateSeq(uint16_t seq)
{
    if (packetsReceived == 0) {
        // First packet: accept unconditionally
        inSeq = seq;
        recvMask = 1;
        return true;
    }

    if (seqGreater(seq, inSeq)) {
        // Newer sequence — advance window
        uint16_t delta = seq - inSeq;
        if (delta >= 64)
            recvMask = 0; // very old window, start fresh
        else
            recvMask <<= delta;
        recvMask |= 1;
        inSeq = seq;
        return true;
    }

    uint16_t delta = inSeq - seq;
    if (delta >= 64)
        return false; // too old

    uint64_t bit = uint64_t(1) << delta;
    if (recvMask & bit)
        return false; // duplicate
    recvMask |= bit;
    return true;
}
