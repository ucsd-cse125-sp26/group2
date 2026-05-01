/// @file OutboundQueue.cpp
/// @brief Implementation of the per-client outbound message queue.

#include "OutboundQueue.hpp"

#include <SDL3/SDL.h>

#include <utility>

void OutboundQueue::enqueue(uint8_t replaceKey, std::vector<uint8_t>&& framedBytes)
{
    const Uint64 now = SDL_GetTicksNS();

    if (replaceKey != 0) {
        // Replace any existing entry with the same key.  The queue is small
        // (≤ a few entries per tick under steady state), so a linear scan is
        // fine — the win is bounded memory under slow-client conditions.
        for (auto& e : entries_) {
            if (e.replaceKey == replaceKey) {
                e.framedBytes = std::move(framedBytes);
                e.enqueuedNs = now;
                return;
            }
        }
    }

    entries_.push_back(OutboundEntry{
        .replaceKey = replaceKey,
        .enqueuedNs = now,
        .framedBytes = std::move(framedBytes),
    });
}

bool OutboundQueue::flushTo(NET_StreamSocket* socket, Uint32 maxAgeMs)
{
    if (!socket)
        return false;

    const Uint64 now = SDL_GetTicksNS();
    const Uint64 maxAgeNs = static_cast<Uint64>(maxAgeMs) * 1'000'000ull;

    while (!entries_.empty()) {
        const OutboundEntry& front = entries_.front();

        // Cull stale unreliable entries.  Reliable entries (replaceKey == 0)
        // always ship — that's the contract: an event is either delivered or
        // we tear the connection down.
        if (maxAgeNs > 0 && front.replaceKey != 0 && (now - front.enqueuedNs) > maxAgeNs) {
            entries_.pop_front();
            continue;
        }

        // SDL3_net's NET_WriteToStreamSocket is non-blocking; it copies into
        // an internal pending_output_buffer if the kernel can't take it now.
        // A `false` return is a real socket error (closed / EPIPE / etc.) —
        // bubble it so the caller can disconnect.
        const auto* data = front.framedBytes.data();
        const auto len = static_cast<int>(front.framedBytes.size());
        if (!NET_WriteToStreamSocket(socket, data, len))
            return false;

        entries_.pop_front();
    }

    return true;
}

size_t OutboundQueue::totalBytes() const noexcept
{
    size_t sum = 0;
    for (const auto& e : entries_)
        sum += e.framedBytes.size();
    return sum;
}
