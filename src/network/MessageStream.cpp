/// @file MessageStream.cpp
/// @brief Implementation of the MessageStream length-prefixed framing layer.

#include "MessageStream.hpp"

#include <cstring>

namespace
{
/// @brief Per-call read buffer size. Larger than the previous 4 KB to reduce
/// loop iterations under heavy load. The drain loop handles arbitrarily large
/// kernel-queued backlogs regardless of this value.
constexpr int k_readChunkBytes = 16 * 1024;

/// @brief Compact recvBuf when the head offset is past this fraction of size.
/// Keeps amortized O(1) per consumed message while preventing unbounded growth
/// on long-lived streams.
constexpr float k_compactThresholdRatio = 0.5f;
constexpr Uint32 k_compactThresholdBytes = 64 * 1024;
} // namespace

bool MessageStream::send(const void* data, Uint32 size)
{
    if (!socket)
        return false;

    auto dataSize = static_cast<int>(size);
    NET_WriteToStreamSocket(socket, &size, sizeof(size));
    return NET_WriteToStreamSocket(socket, data, dataSize);
}

void MessageStream::recvCompact()
{
    if (recvHead == 0)
        return;
    const Uint32 available = recvAvailable();
    if (available > 0) {
        std::memmove(recvBuf.data(), recvBuf.data() + recvHead, available);
    }
    recvBuf.resize(available);
    recvHead = 0;
}

bool MessageStream::poll(const std::function<void(const void* data, Uint32 size)>& callback)
{
    if (!socket)
        return false;

    // ── 1. Drain the kernel receive buffer fully ──────────────────────────
    //
    // Loop until the socket reports zero bytes available. Without this, a
    // single poll() call only pulls k_readChunkBytes per game-thread tick,
    // capping the client's drain rate. When the server produces faster than
    // that, the kernel buffer accumulates indefinitely → snapshots queue up
    // → PONG bytes get stuck behind them → measured ping spikes (the new-
    // client ping-decay symptom).
    //
    // NET_ReadFromStreamSocket is non-blocking: returns 0 when the kernel
    // buffer is empty, >0 with the byte count, <0 on socket error.
    Uint8 buf[k_readChunkBytes];
    for (;;) {
        const int n = NET_ReadFromStreamSocket(socket, buf, sizeof(buf));
        if (n < 0)
            return false; // socket error → caller treats as disconnect
        if (n == 0)
            break;        // kernel buffer empty → done draining
        recvBuf.insert(recvBuf.end(), buf, buf + n);
    }

    // ── 2. Drain complete framed messages out of recvBuf ──────────────────
    //
    // Use a head offset rather than vector::erase(begin,...) which is O(N)
    // and turns "drain 10 backlogged messages of M bytes each" into O(N^2)
    // memmove work — a self-amplifying lag spiral. We compact only when the
    // head has consumed enough of the buffer to be worth the move.
    while (recvAvailable() >= sizeof(Uint32)) {
        Uint32 len;
        std::memcpy(&len, recvFront(), sizeof(Uint32));

        if (recvAvailable() < sizeof(Uint32) + len)
            break; // need more data

        const Uint8* payload = recvFront() + sizeof(Uint32);
        callback(payload, len);

        recvHead += sizeof(Uint32) + len;
    }

    // ── 3. Compact when worthwhile ───────────────────────────────────────
    //
    // Cap memory growth without paying compact cost on every consumed
    // message. The thresholds bias toward "compact rarely, but always when
    // the buffer gets big".
    if (recvHead > k_compactThresholdBytes ||
        (recvBuf.size() > 0 &&
         static_cast<float>(recvHead) / static_cast<float>(recvBuf.size()) > k_compactThresholdRatio))
    {
        recvCompact();
    }

    return true;
}
