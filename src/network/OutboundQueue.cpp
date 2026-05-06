/// @file OutboundQueue.cpp
/// @brief Implementation of the per-client outbound message queue.

#include "OutboundQueue.hpp"

#include <SDL3/SDL.h>

#include <utility>

void OutboundQueue::enqueue(uint8_t replaceKey, std::shared_ptr<const std::vector<uint8_t>> framedBytes)
{
    if (!framedBytes)
        return;

    const Uint64 now = SDL_GetTicksNS();

    // PR-5b: hold the mutex for the whole enqueue. The replace-key
    // scan and push_back both touch `entries_`, which can be flushed
    // concurrently by the network thread.
    std::lock_guard<std::mutex> lock(mutex_);

    if (replaceKey != 0) {
        // Replace any existing entry with the same key. PR-2: storing a
        // shared_ptr means the replace path is now a pointer assignment
        // instead of a vector move + heap free.
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

void OutboundQueue::enqueue(uint8_t replaceKey, std::vector<uint8_t>&& framedBytes)
{
    // Wrap the rvalue vector exactly once. The cost is one heap-alloc for
    // the shared_ptr control block; the vector itself is moved into the
    // shared owner in-place (no byte copy). Callers on broadcast paths
    // (PR-2) construct the shared_ptr upstream so this overload is mostly
    // for single-client paths (notifyPlayerClientId, ASSIGN_CLIENT_ID,
    // PONG, etc.).
    auto shared = std::make_shared<std::vector<uint8_t>>(std::move(framedBytes));
    enqueue(replaceKey, std::shared_ptr<const std::vector<uint8_t>>{std::move(shared)});
}

bool OutboundQueue::flushTo(NET_StreamSocket* socket, Uint32 maxAgeMs)
{
    if (!socket)
        return false;

    // PR-5b: drain entries one at a time, holding the mutex only
    // for the deque pop and copy-out. The actual `NET_WriteToStreamSocket`
    // syscall runs WITHOUT the mutex held — so a game-thread enqueue on
    // the same client can land between this client's flushed entries.
    // (SDL3_net's NET_WriteToStreamSocket is itself thread-safe per
    // SDL_net's contract: it just dispatches into the socket's
    // pending_output_buffer.)
    const Uint64 now = SDL_GetTicksNS();
    const Uint64 maxAgeNs = static_cast<Uint64>(maxAgeMs) * 1'000'000ull;

    while (true) {
        OutboundEntry entry;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (entries_.empty())
                return true;

            // Cull stale unreliable entries.  Reliable entries (replaceKey == 0)
            // always ship — that's the contract: an event is either delivered or
            // we tear the connection down.
            if (maxAgeNs > 0 && entries_.front().replaceKey != 0 && (now - entries_.front().enqueuedNs) > maxAgeNs) {
                entries_.pop_front();
                continue;
            }

            // Move out of the deque before unlocking; the shared_ptr keeps
            // the buffer alive for the syscall below.
            entry = std::move(entries_.front());
            entries_.pop_front();
        }

        if (!entry.framedBytes) {
            // Defensive: a null pointer slipped through. Skip.
            continue;
        }

        const auto* data = entry.framedBytes->data();
        const auto len = static_cast<int>(entry.framedBytes->size());
        if (!NET_WriteToStreamSocket(socket, data, len))
            return false;
    }
}

size_t OutboundQueue::totalBytes() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t sum = 0;
    for (const auto& e : entries_)
        sum += e.framedBytes ? e.framedBytes->size() : 0;
    return sum;
}
