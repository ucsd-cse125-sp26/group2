/// @file MessageStream.hpp
/// @brief Length-prefixed message framing layer over a TCP stream socket.

#pragma once

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <functional>
#include <vector>

/// @brief Length-prefixed framing layer over a TCP stream socket.
///
/// Wraps a raw NET_StreamSocket and handles splitting the byte stream into
/// discrete messages. Each message is sent/received with a 4-byte
/// (host-endian) length header followed by the payload bytes.
///
/// @note Receive draining: poll() loops NET_ReadFromStreamSocket until the
/// kernel buffer is empty (returns 0). This is essential because each call
/// to poll() must drain whatever the OS has queued, otherwise a snapshot
/// stream that produces faster than the consumer reads will accumulate
/// indefinitely in the kernel buffer (head-of-line blocking). See
/// docs/networking.md.
///
/// @note recvBuf uses a head-offset compaction strategy instead of front-erase
/// so draining N queued messages is O(N) total rather than O(N^2). The
/// buffer is compacted (offset reset to 0) only when a threshold is crossed.
class MessageStream
{

public:
    /// @brief PR-5b: default-construct so server `Connection` (which
    /// embeds a `MessageStream`) can be `try_emplace`'d into the
    /// clients map. The actual socket is assigned right after via
    /// the `MessageStream(NET_StreamSocket*)` form.
    MessageStream() = default;

    MessageStream(NET_StreamSocket* sock) : socket(sock) {}

    NET_StreamSocket* socket = nullptr; ///< Underlying SDL_net stream socket.

    /// @brief Send a framed message over the socket.
    /// @param data  Pointer to the payload bytes.
    /// @param size  Payload length in bytes.
    /// @return False if the send fails.
    bool send(const void* data, Uint32 size);

    /// @brief Read all available bytes from the socket and invoke the callback
    ///        once per complete message frame.
    /// @param callback  Called with a pointer and length for each complete message.
    /// @return False if the socket reports an error.
    bool poll(const std::function<void(const void* data, Uint32 size)>& callback);

    /// @brief Read-only half of poll(): drain the kernel into recvBuf.
    ///
    /// Stage 3c uses this from a dedicated network thread that owns kernel
    /// I/O. The corresponding `drainComplete` runs on the game thread to
    /// dispatch fully-received messages — both halves take a mutex around
    /// recvBuf so they don't race.
    ///
    /// @return False on socket error (caller should disconnect).
    bool pumpReads();

    /// @brief Decode-only half of poll(): invoke the callback for each
    ///        complete message currently buffered.
    ///
    /// Stage 3c uses this from the game thread after `pumpReads` has run on
    /// the network thread. Together with pumpReads it does what poll() does
    /// in the single-threaded code path, just split across two threads.
    void drainComplete(const std::function<void(const void* data, Uint32 size)>& callback);

private:
    /// @brief Bytes available for parsing in recvBuf, starting at recvHead.
    [[nodiscard]] Uint32 recvAvailable() const noexcept { return static_cast<Uint32>(recvBuf.size()) - recvHead; }

    /// @brief Pointer to the first unconsumed byte.
    [[nodiscard]] const Uint8* recvFront() const noexcept { return recvBuf.data() + recvHead; }

    /// @brief Compact recvBuf: shift unconsumed bytes to the front, reset head.
    void recvCompact();

    std::vector<Uint8> recvBuf; ///< Accumulates partial data between poll() calls.
    Uint32 recvHead = 0;        ///< Offset of next unconsumed byte in recvBuf (avoids O(N) erase).
};
