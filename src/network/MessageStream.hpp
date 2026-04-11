/// @file MessageStream.hpp
/// @brief Length-prefixed message framing layer over a TCP stream socket.

#pragma once

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <cstring>
#include <functional>
#include <vector>

/// @brief Length-prefixed framing layer over a TCP stream socket.
///
/// Wraps a raw NET_StreamSocket and handles splitting the byte stream into
/// discrete messages. Each message is sent/received with a 4-byte big-endian
/// length header followed by the payload bytes.
class MessageStream
{

public:
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

private:
    std::vector<Uint8> recvBuf; ///< Accumulates partial data between poll() calls.
};
