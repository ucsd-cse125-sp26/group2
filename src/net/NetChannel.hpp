#pragma once
#include "Protocol.hpp"
#include "Socket.hpp"

#include <array>
#include <cstring>
#include <functional>

// ---------------------------------------------------------------------------
// NetChannel — thin layer over UdpSocket that adds:
//   • Packet sequencing (detect stale / out-of-order packets)
//   • Acknowledge tracking (ack + 16-bit ackBits bitmask)
//   • Basic duplicate rejection
//
// Does NOT implement reliable delivery for state packets (snapshots/inputs
// are "fire and forget" — the game recovers naturally from loss).
// Critical one-time messages (Connect, ConnectAck, Kick) should be re-sent
// by the caller until acknowledged.
// ---------------------------------------------------------------------------

class NetChannel
{
public:
    // Bind a socket and peer address to this channel.
    // For the SERVER side, peerAddr is unknown at construction; call
    // setPeer() when a client connects.  For the CLIENT side, peerAddr is
    // the server address.
    void init(UdpSocket* socket, const sockaddr_in* peerAddr = nullptr);
    void setPeer(const sockaddr_in& addr)
    {
        peer = addr;
        hasPeer = true;
    }
    const sockaddr_in& getPeer() const { return peer; }
    bool hasPeerAddr() const { return hasPeer; }

    // ---------------------------------------------------------------------------
    // Outgoing
    // ---------------------------------------------------------------------------

    // Fill the header fields on a packet and send it.
    // Returns bytes sent or -1.
    int send(void* packet, int totalBytes, PacketType type, uint8_t clientId = 0xFF);

    // ---------------------------------------------------------------------------
    // Incoming
    // ---------------------------------------------------------------------------

    // Read one packet from the socket into buf (maxLen bytes).
    // Returns: > 0 = bytes received (header validated, sequence checked)
    //            0 = nothing available or stale duplicate
    //           -1 = socket error
    // outFrom is set to sender's address when > 0 is returned.
    int recv(void* buf, int maxLen, sockaddr_in& outFrom);

    // ---------------------------------------------------------------------------
    // Stats
    // ---------------------------------------------------------------------------

    uint32_t packetsSent = 0;
    uint32_t packetsReceived = 0;
    uint32_t packetsDropped = 0; // stale / duplicate

private:
    UdpSocket* sock = nullptr;
    sockaddr_in peer = {};
    bool hasPeer = false;

    uint16_t outSeq = 0; // next outgoing sequence
    uint16_t inSeq = 0;  // last accepted incoming sequence
    uint16_t inAck = 0;  // last sequence WE received (sent back to peer)
    uint16_t inBits = 0; // ack bitmask for inAck-1 .. inAck-16

    // Track which incoming sequences we've seen (duplicate detection).
    // 64-bit bitmask covers the last 64 sequences.
    uint64_t recvMask = 0;

    bool checkAndUpdateSeq(uint16_t seq);
};
