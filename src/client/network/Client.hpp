#pragma once

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>

/// @brief UDP datagram client — sends input to the server and receives state updates.
class Client
{
public:
    /// @brief Create the UDP socket and resolve the server address.
    /// @param addr  Hostname or IP address of the server.
    /// @param port  UDP port the server is listening on.
    /// @return False on socket creation or DNS failure.
    bool init(const char* addr, Uint16 port);

    /// @brief Close the socket and release the resolved address.
    void shutdown();

    /// @brief Send a raw datagram to the server.
    /// @param data  Pointer to the payload bytes.
    /// @param size  Payload length in bytes.
    /// @return False if the send fails.
    bool send(const void* data, int size);

    /// @brief Receive and process one pending datagram.
    /// @return True if a datagram was received, false if the queue is empty.
    bool poll();

private:
    NET_DatagramSocket* sock = nullptr; ///< Bound UDP socket.
    NET_Address* serverAddr = nullptr;  ///< Resolved server address.
    Uint16 serverPort = 0;              ///< Server UDP port.
};
