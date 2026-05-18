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
constexpr Uint32 k_maxFrameBytes = 2 * 1024 * 1024;
constexpr std::size_t k_maxRecvBufferBytes = static_cast<std::size_t>(k_maxFrameBytes) + k_readChunkBytes;

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
    if (size > k_maxFrameBytes)
        return false;

    auto dataSize = static_cast<int>(size);
    if (!NET_WriteToStreamSocket(socket, &size, sizeof(size)))
        return false;
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

bool MessageStream::pumpReads()
{
    if (!socket)
        return false;

    // ── Drain the kernel receive buffer fully ──────────────────────────
    //
    // Loop until the socket reports zero bytes available. Without this, a
    // single call only pulls k_readChunkBytes per invocation, capping the
    // drain rate. When the server produces faster than that, the kernel
    // buffer accumulates indefinitely → snapshots queue up → PONG bytes
    // get stuck behind them → measured ping spikes (the new-client
    // ping-decay symptom).
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
        if (recvBuf.size() + static_cast<std::size_t>(n) > k_maxRecvBufferBytes)
            return false;
        recvBuf.insert(recvBuf.end(), buf, buf + n);
    }
    return true;
}

bool MessageStream::drainComplete(const std::function<void(const void* data, Uint32 size)>& callback)
{
    // Use a head offset rather than vector::erase(begin,...) which is O(N)
    // and turns "drain 10 backlogged messages of M bytes each" into O(N^2)
    // memmove work — a self-amplifying lag spiral. We compact only when the
    // head has consumed enough of the buffer to be worth the move.
    while (recvAvailable() >= sizeof(Uint32)) {
        Uint32 len;
        std::memcpy(&len, recvFront(), sizeof(Uint32));
        if (len > k_maxFrameBytes) {
            recvBuf.clear();
            recvHead = 0;
            return false;
        }

        if (static_cast<std::size_t>(recvAvailable()) < sizeof(Uint32) + static_cast<std::size_t>(len))
            break; // need more data

        const Uint8* payload = recvFront() + sizeof(Uint32);
        callback(payload, len);

        recvHead += sizeof(Uint32) + len;
    }

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

bool MessageStream::poll(const std::function<void(const void* data, Uint32 size)>& callback)
{
    // Single-threaded convenience: pump the kernel into recvBuf, then
    // dispatch any complete frames. Stage 3c splits these phases across
    // a network thread (pumpReads) and the game thread (drainComplete).
    if (!pumpReads())
        return false;
    return drainComplete(callback);
}
