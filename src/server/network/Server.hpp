#pragma once

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>

/// @brief UDP datagram socket — receives client packets and echoes them back.
///
/// Call poll() every tick to drain incoming datagrams.
/// Extend handleDatagram() with proper packet dispatch as the game protocol grows.
class Server
{
public:
    /// @brief Bind a UDP socket to the given address and port.
    /// @param addr  Hostname or IP to bind to (e.g. "127.0.0.1").
    /// @param port  UDP port to listen on.
    /// @return False on DNS or socket creation failure.
    bool init(const char* addr, Uint16 port);

    /// @brief Close the socket and release resources.
    void shutdown();

    /// @brief Drain all pending datagrams for this tick.
    void poll();

private:
    /// @brief Process a single received datagram (currently echo-back only).
    /// @param dgram  The received datagram (caller retains ownership).
    void handleDatagram(NET_Datagram* dgram);

    NET_DatagramSocket* sock = nullptr; ///< Bound UDP socket.
};
