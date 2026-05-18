/// @file UdpEndpoint.hpp
/// @brief Thin wrapper over SDL3_net's NET_DatagramSocket.
///
/// Phase 3d-1 introduces this as the foundation for the UDP transport.
/// It owns one UDP socket (server-bound or client-bound), provides
/// header-prefixed send/receive of payloads, and tracks connection IDs
/// for the server-side address ↔ client mapping.
///
/// **Why not just use NET_SendDatagram / NET_ReceiveDatagram directly?**
/// Because every datagram needs the 16-byte PacketHeader prepended
/// (with magic/version/connectionId/sequence/channel) and validated on
/// receive. UdpEndpoint does that automatically so callers work with
/// payloads, not raw datagrams.
///
/// **Threading**: the SDL_net docs state datagram sockets are safe to
/// call from one thread at a time. The Server's existing network thread
/// is the natural owner; the game thread enqueues sends and pops
/// received messages via stateMutex_-protected paths in Server.cpp.
///
/// **Connection IDs**: established over the existing TCP path during
/// `notifyPlayerClientId`. The TCP handshake hands the client a 32-bit
/// random `connectionId` and the client stamps every UDP datagram with
/// it. The server demuxes incoming UDP by connectionId → ClientId.
/// This skips the full 4-step UDP handshake from the original plan;
/// works because TCP is already establishing the connection. The full
/// handshake is a Phase 3d-future enhancement (NAT traversal, no-TCP
/// support).

#pragma once

#include "PacketHeader.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <cstdint>
#include <vector>

namespace net
{

/// @brief A UDP datagram address (server's view of a remote peer).
///
/// SDL3_net stores the (addr, port) pair as (NET_Address*, Uint16). We
/// hold a refcounted pointer to the address so the same value survives
/// across `NET_ReceiveDatagram` → `NET_DestroyDatagram` cycles. The
/// caller owns the lifetime — call `release()` when discarding.
struct UdpEndpointAddr
{
    NET_Address* addr = nullptr; ///< Refcounted SDL_net address.
    Uint16 port = 0;             ///< Source/destination port.

    UdpEndpointAddr() = default;
    UdpEndpointAddr(NET_Address* address, Uint16 p) : addr(address ? NET_RefAddress(address) : nullptr), port(p) {}
    UdpEndpointAddr(const UdpEndpointAddr& other)
        : addr(other.addr ? NET_RefAddress(other.addr) : nullptr), port(other.port)
    {}
    UdpEndpointAddr& operator=(const UdpEndpointAddr& other)
    {
        if (this == &other)
            return *this;
        release();
        addr = other.addr ? NET_RefAddress(other.addr) : nullptr;
        port = other.port;
        return *this;
    }
    UdpEndpointAddr(UdpEndpointAddr&& other) noexcept : addr(other.addr), port(other.port)
    {
        other.addr = nullptr;
        other.port = 0;
    }
    UdpEndpointAddr& operator=(UdpEndpointAddr&& other) noexcept
    {
        if (this == &other)
            return *this;
        release();
        addr = other.addr;
        port = other.port;
        other.addr = nullptr;
        other.port = 0;
        return *this;
    }
    ~UdpEndpointAddr() { release(); }

    /// @brief Drop the refcount (idempotent).
    void release() noexcept
    {
        if (addr) {
            NET_UnrefAddress(addr);
            addr = nullptr;
        }
    }
};

/// @brief One framed UDP message ready to dispatch to upper layers.
struct UdpReceivedMessage
{
    UdpEndpointAddr from;         ///< Source address. Owns the NET_Address ref.
    PacketHeader header{};        ///< Parsed header.
    std::vector<uint8_t> payload; ///< Bytes after the header.
};

/// @brief Wraps a NET_DatagramSocket with header-prefixed I/O.
///
/// Same class is used in both server mode (binds to a port) and client
/// mode (binds to any free port). Distinction is which @ref open
/// overload you call.
class UdpEndpoint
{
public:
    UdpEndpoint() = default;
    UdpEndpoint(const UdpEndpoint&) = delete;
    UdpEndpoint& operator=(const UdpEndpoint&) = delete;
    UdpEndpoint(UdpEndpoint&&) = delete;
    UdpEndpoint& operator=(UdpEndpoint&&) = delete;
    ~UdpEndpoint() { close(); }

    /// @brief Bind a UDP socket to @p bindAddr:@p port (server) or to any
    /// free port (client, with @p bindAddr = nullptr and @p port = 0).
    /// @return False on bind / DNS / socket-creation failure.
    bool open(const char* bindAddr, Uint16 port);

    /// @brief Close the socket. Idempotent.
    void close() noexcept;

    /// @brief Send a payload with our header prefixed, to @p dest.
    ///
    /// Constructs the on-the-wire datagram = `[PacketHeader][payload]`.
    /// Caller fills the relevant header fields (channel / sequence /
    /// connectionId) — magic, version, kind=Payload, _pad are filled here.
    ///
    /// @return False on socket error or oversize payload.
    bool send(const UdpEndpointAddr& dest, PacketHeader hdr, const void* payload, int payloadLen);

    /// @brief Send an already encoded datagram to @p dest.
    ///
    /// Used by relay wrappers and tests that need the complete
    /// `[PacketHeader][payload]` byte sequence as an opaque blob.
    bool sendDatagramBytes(const UdpEndpointAddr& dest, const void* bytes, int len);

    /// @brief Send a payload by splitting it into MTU-safe fragments.
    ///
    /// Stage 3d-4: snapshots at 100 players are ~5 KB, well over the
    /// MTU-safe `k_maxPayloadBytes` (~1184 bytes per single datagram).
    /// This helper splits @p data into ceil(len / k_maxPayloadBytes)
    /// fragments. Each fragment carries the same `(channel, sequence)`
    /// in its PacketHeader plus the bit-0-set `flags.fragmented`, with
    /// `fragmentInfo` packing `(index << 8) | count`.
    ///
    /// The receiver pairs them by `(connectionId, sequence)` in a
    /// FragmentReassembler. Drop-stale: a newer sequence supersedes any
    /// in-progress reassembly. Single dropped fragment loses the whole
    /// snapshot — fine because the next snapshot lands ~31 ms later.
    ///
    /// @param dest        Destination address.
    /// @param hdr         Caller-supplied header. `flags` and
    ///                    `fragmentInfo` are overwritten per-fragment.
    /// @param data        Payload bytes (split internally).
    /// @param dataLen     Total payload length.
    /// @param redundancy PR-15 (server-perf): send each individual
    ///         fragment this many times back-to-back.  Receivers'
    ///         `FragmentReassembler` deduplicates by (sequence,
    ///         fragmentIndex), so duplicate copies just fill any gaps
    ///         left by single-copy losses.  Default 1 = no redundancy
    ///         (legacy behaviour).  At `redundancy = 2` and 5 % per-
    ///         fragment loss, P(any single fragment lost) drops from
    ///         5 % → 5 %² = 0.25 %; for a 9-fragment FULL keyframe,
    ///         P(at least one fragment fully lost) goes 37 % → 2 %.
    ///         Used for FULL keyframes which can't tolerate any
    ///         fragment loss (the entire snapshot is then unusable);
    ///         DELTAs default to redundancy=1 since a single dropped
    ///         delta only costs one frame's worth of state at PR-14's
    ///         per-keyframe baseline.
    /// @return False on socket error or if the payload would need more
    ///         than 256 fragments (sanity cap; 256 × ~1.18 KB ≈ 302 KB
    ///         logical-message ceiling, vastly more than we'll need).
    bool
    sendFragmented(const UdpEndpointAddr& dest, PacketHeader hdr, const void* data, int dataLen, int redundancy = 1);

    /// @brief Try to receive one datagram (non-blocking).
    /// @param out Filled in on success.
    /// @return True if a datagram was received and parsed; false if the
    ///         queue was empty or the datagram was malformed (in which
    ///         case it's silently dropped — UDP is best-effort).
    bool tryReceive(UdpReceivedMessage& out);

    /// @brief True after a successful @ref open.
    [[nodiscard]] bool isOpen() const noexcept { return socket_ != nullptr; }

private:
    NET_DatagramSocket* socket_ = nullptr;
};

} // namespace net
