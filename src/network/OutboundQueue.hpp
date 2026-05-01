/// @file OutboundQueue.hpp
/// @brief Per-client outbound message queue with replace-on-stale semantics.
///
/// SDL3_net's `NET_WriteToStreamSocket` is already non-blocking and
/// internally queues bytes that don't immediately go to the kernel — but
/// that queue grows unbounded for slow clients, and once a snapshot is in
/// it we lose all ability to supersede it with a fresher one. With 100
/// connected bots and the server producing ~20 KB of registry snapshot
/// every tick, a slow drainer ends up with dozens of stale snapshots
/// stacked behind the first one in flight. Each one bytes-equivalent of
/// data the client still has to receive and decode before reaching the
/// state we actually want them at.
///
/// This class sits one layer above the SDL_net send queue and bounds it:
///
///   * @b replace-on-stale: enqueueing a packet with a non-zero
///     `replaceKey` removes any previously queued entry with the same key
///     before pushing. Snapshots use `replaceKey =
///     PacketType::UPDATE_REGISTRY`, so a slow client only ever has one
///     snapshot pending — always the freshest.
///   * @b max-age culling: optional. Entries older than @p maxAgeMs are
///     dropped on flush. Reliable-style messages (events) opt out by
///     using `replaceKey == 0`, which marks them as "must ship".
///
/// Phase-3 staging: this is intentionally a small, single-threaded wrapper
/// around a std::deque. Stage 3b moves the flush onto a dedicated network
/// thread and makes the enqueue cross-thread via an SPSC ring; the public
/// API here doesn't need to change.

#pragma once

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <cstdint>
#include <deque>
#include <vector>

/// @brief Bytes already framed (4-byte length prefix + payload) ready for the wire.
///
/// `framedBytes` is the exact byte sequence that `MessageStream::send`
/// would have produced — held by-value so the producer can reuse its
/// scratch buffer immediately after enqueueing.
struct OutboundEntry
{
    /// @brief Replace-key. 0 means "always append" (events: KILL, PARTICLE,
    /// PONG, ASSIGN_CLIENT_ID). Non-zero means "replace any existing
    /// entry with the same key before pushing" — used for snapshot-style
    /// state where only the latest is meaningful.
    uint8_t replaceKey = 0;

    /// @brief Microsecond timestamp at enqueue time, used for max-age culling.
    Uint64 enqueuedNs = 0;

    /// @brief Pre-framed bytes including the 4-byte length prefix.
    std::vector<uint8_t> framedBytes;
};

/// @brief Per-connection outbound message queue.
///
/// Not thread-safe; each Connection owns one. The Server's broadcast
/// helpers mutate it on the game thread; in stage 3b a dedicated network
/// thread will drain it. For stage 3a everything runs on the game thread.
class OutboundQueue
{
public:
    /// @brief Enqueue a framed message.
    ///
    /// @param replaceKey  See @ref OutboundEntry::replaceKey.
    /// @param framedBytes Bytes to send. **Must** already include the
    ///                    4-byte length prefix that MessageStream uses for
    ///                    framing — this class doesn't add it.
    void enqueue(uint8_t replaceKey, std::vector<uint8_t>&& framedBytes);

    /// @brief Drain the queue, writing each entry to @p socket.
    ///
    /// Entries older than @p maxAgeMs are dropped *if* their replaceKey
    /// is non-zero (i.e. unreliable-style snapshot/state). Reliable
    /// entries with replaceKey == 0 are always shipped regardless of age.
    ///
    /// @param socket   SDL3_net stream socket to write to.
    /// @param maxAgeMs Drop unreliable entries older than this (0 = no
    ///                 culling). Plan default: 300 ms.
    /// @return False on socket error (caller should disconnect the client).
    bool flushTo(NET_StreamSocket* socket, Uint32 maxAgeMs);

    /// @brief Number of entries currently queued (for telemetry / tests).
    [[nodiscard]] size_t depth() const noexcept { return entries_.size(); }

    /// @brief Total bytes across all queued entries (for telemetry).
    [[nodiscard]] size_t totalBytes() const noexcept;

    /// @brief Drop all queued entries (used on disconnect).
    void clear() noexcept { entries_.clear(); }

private:
    std::deque<OutboundEntry> entries_;
};
