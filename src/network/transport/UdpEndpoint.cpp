/// @file UdpEndpoint.cpp
/// @brief Implementation of the SDL3_net UDP datagram wrapper.

#include "UdpEndpoint.hpp"

#include <SDL3/SDL.h>

#include <cstring>

namespace net
{

bool UdpEndpoint::open(const char* bindAddr, Uint16 port)
{
    if (socket_) {
        SDL_Log("UdpEndpoint::open called on already-open socket");
        return false;
    }

    NET_Address* netAddr = nullptr;
    if (bindAddr) {
        netAddr = NET_ResolveHostname(bindAddr);
        if (NET_WaitUntilResolved(netAddr, -1) == NET_FAILURE) {
            SDL_Log("UdpEndpoint: failed to resolve bind address '%s': %s", bindAddr, SDL_GetError());
            NET_UnrefAddress(netAddr);
            return false;
        }
    }
    // null address → server-mode "bind to all interfaces" or client-mode
    // "any free port". SDL3_net handles both.

    socket_ = NET_CreateDatagramSocket(netAddr, port);
    if (netAddr)
        NET_UnrefAddress(netAddr);

    if (!socket_) {
        SDL_Log("UdpEndpoint: NET_CreateDatagramSocket failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void UdpEndpoint::close() noexcept
{
    if (socket_) {
        NET_DestroyDatagramSocket(socket_);
        socket_ = nullptr;
    }
}

bool UdpEndpoint::send(const UdpEndpointAddr& dest, PacketHeader hdr, const void* payload, int payloadLen)
{
    if (!socket_)
        return false;
    if (payloadLen < 0 || payloadLen > k_maxPayloadBytes) {
        SDL_Log("UdpEndpoint::send: payload size %d out of range [0, %d]", payloadLen, k_maxPayloadBytes);
        return false;
    }
    if (!dest.addr) {
        SDL_Log("UdpEndpoint::send: null dest address");
        return false;
    }

    // Fill the framework-managed fields. Channel/sequence/connectionId
    // are caller's responsibility (they're set per-packet upstream).
    hdr.magic = k_protocolMagic;
    hdr.version = k_protocolVersion;
    hdr._pad = 0;

    // Single contiguous datagram: [PacketHeader][payload].
    uint8_t buf[k_maxPacketBytes];
    encodePacketHeader(hdr, buf);
    if (payloadLen > 0)
        std::memcpy(buf + sizeof(hdr), payload, static_cast<size_t>(payloadLen));

    return NET_SendDatagram(socket_, dest.addr, dest.port, buf, static_cast<int>(sizeof(hdr)) + payloadLen);
}

bool UdpEndpoint::sendDatagramBytes(const UdpEndpointAddr& dest, const void* bytes, int len)
{
    if (!socket_)
        return false;
    if (!dest.addr) {
        SDL_Log("UdpEndpoint::sendDatagramBytes: null dest address");
        return false;
    }
    if (!bytes || len < static_cast<int>(sizeof(PacketHeader)) || len > k_maxPacketBytes) {
        SDL_Log("UdpEndpoint::sendDatagramBytes: datagram size %d out of range [%zu, %d]",
                len,
                sizeof(PacketHeader),
                k_maxPacketBytes);
        return false;
    }
    return NET_SendDatagram(socket_, dest.addr, dest.port, bytes, len);
}

bool UdpEndpoint::sendFragmented(
    const UdpEndpointAddr& dest, PacketHeader hdr, const void* data, int dataLen, int redundancy)
{
    if (!socket_ || dataLen < 0)
        return false;
    if (redundancy < 1)
        redundancy = 1;

    // Single-datagram fast path. No fragmentation overhead when the
    // payload already fits.  Redundancy still applies: at redundancy=2
    // we send the lone datagram twice, doubling delivery probability
    // for small but critical messages.
    if (dataLen <= k_maxPayloadBytes) {
        hdr.flags = 0;
        hdr.fragmentInfo = 0;
        bool ok = true;
        for (int copy = 0; copy < redundancy; ++copy) {
            if (!send(dest, hdr, data, dataLen))
                ok = false;
        }
        return ok;
    }

    // Fragment count = ceil(dataLen / k_maxPayloadBytes). The low byte
    // of fragmentInfo carries the count, so 255 is the maximum encodable
    // value.
    const int fragCount = (dataLen + k_maxPayloadBytes - 1) / k_maxPayloadBytes;
    if (fragCount > 255) {
        SDL_Log("UdpEndpoint::sendFragmented: payload %d B needs %d fragments (>255 cap)", dataLen, fragCount);
        return false;
    }

    // PR-15: outer loop is fragments, inner is redundancy.  Each
    // fragment goes out `redundancy` times back-to-back with identical
    // header bytes (same sequence, same fragmentInfo).  Receiver's
    // FragmentReassembler treats duplicates idempotently — see line 103
    // of FragmentReassembler.hpp — so the redundant copies just fill
    // any slots the primary copies failed to deliver.
    //
    // Why fragment-major instead of copy-major?  Two reasons:
    //   1. Cache locality: each fragment's bytes are touched twice
    //      back-to-back rather than once-per-fragment-then-rewound.
    //   2. Burst-loss correlation: if the network drops a few adjacent
    //      datagrams together, fragment-major spreads the redundant
    //      copies across the same correlated drop window, while
    //      copy-major would put them in the same drop window only at
    //      the seam between copies.  Fragment-major is more resilient
    //      to bursty loss, which is the realistic case for kernel
    //      send-buffer overflow.
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (int i = 0; i < fragCount; ++i) {
        const int offset = i * k_maxPayloadBytes;
        const int chunkLen = std::min(k_maxPayloadBytes, dataLen - offset);

        PacketHeader fragHdr = hdr;
        fragHdr.flags = static_cast<uint8_t>(fragHdr.flags | k_flagFragmented);
        fragHdr.fragmentInfo = static_cast<uint16_t>((i << 8) | fragCount);

        for (int copy = 0; copy < redundancy; ++copy) {
            if (!send(dest, fragHdr, bytes + offset, chunkLen)) {
                // Stop early on send failure.  Caller will get a partial
                // set on the wire; the receiver's FragmentReassembler will
                // time it out.  UDP is best-effort; this matches the spec.
                return false;
            }
        }
    }
    return true;
}

bool UdpEndpoint::tryReceive(UdpReceivedMessage& out)
{
    if (!socket_)
        return false;

    NET_Datagram* dgram = nullptr;
    if (!NET_ReceiveDatagram(socket_, &dgram)) {
        // Fatal socket error per SDL_net docs; UDP normally just returns
        // (true, nullptr) on no-data.
        return false;
    }
    if (!dgram)
        return false;

    // Validate: must be at least header-sized, and magic/version must match.
    if (dgram->buflen < static_cast<int>(sizeof(PacketHeader))) {
        NET_DestroyDatagram(dgram);
        return false;
    }

    PacketHeader hdr;
    if (!decodePacketHeader(dgram->buf, static_cast<std::size_t>(dgram->buflen), hdr)) {
        // Not for us — silently drop. UDP receives noise sometimes.
        NET_DestroyDatagram(dgram);
        return false;
    }

    out.header = hdr;
    const int payloadLen = dgram->buflen - static_cast<int>(sizeof(hdr));
    if (payloadLen > 0) {
        out.payload.assign(dgram->buf + sizeof(hdr), dgram->buf + dgram->buflen);
    } else {
        out.payload.clear();
    }

    // Take a ref on the address so the caller can hold it past the
    // NET_DestroyDatagram below. They release() it when done.
    if (out.from.addr)
        out.from.release();
    out.from.addr = NET_RefAddress(dgram->addr);
    out.from.port = dgram->port;

    NET_DestroyDatagram(dgram);
    return true;
}

} // namespace net
