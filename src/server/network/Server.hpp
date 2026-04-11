/// @file Server.hpp
/// @brief TCP game server that accepts clients and dispatches incoming packets.

#pragma once

#include "network/MessageStream.hpp"
#include "systems/EventQueue.hpp"

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>
#include <vector>

/// @brief TCP stream socket — receives client packets and echoes them back.
///
/// Call poll() every tick to drain incoming messages.
/// Extend handleMessage() with proper packet dispatch as the game protocol grows.
class Server
{
public:
    /// @brief Bind a TCP socket to the given address and port.
    /// @param addr  Hostname or IP to bind to (e.g. "127.0.0.1").
    /// @param port  TCP port to listen on.
    /// @return False on DNS or socket creation failure.
    bool init(const char* addr, Uint16 port);

    /// @brief Close the socket and release resources.
    void shutdown();

    /// @brief Drain all pending messages for this tick.
    void poll();

    /// @brief Accept up to one new client connection per call.
    void acceptClients();

    /// @brief Read and process pending messages from all connected clients.
    void readClients();

    /// @brief Check whether the event queue is empty.
    /// @return True if no events are pending.
    bool isEmpty();

    /// @brief Remove and return the next event from the queue.
    /// @return The front event.
    Event dequeueEvent();

private:
    /// @brief Per-client connection state.
    struct Connection
    {
        MessageStream msgStream; ///< Framed message stream for this client.
        uint8_t clientId;        ///< Unique identifier assigned on accept.
    };

    /// @brief Dispatch a single decoded message from a client.
    /// @param client The connection the message arrived on.
    /// @param data   Pointer to the message payload.
    /// @param len    Payload length in bytes.
    void handleMessage(Connection& client, const void* data, Uint32 len);

    NET_Server* server = nullptr;    ///< Underlying SDL_net server handle.

    std::vector<Connection> clients; ///< Currently connected clients.
    EventQueue eventQueue;           ///< Incoming events awaiting processing.

    uint8_t nextClientId = 0;        ///< Counter for assigning client IDs.
};
