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

    void acceptClients();
    void readClients();

    bool isEmpty();
    Event dequeueEvent();

private:
    struct Connection
    {
        MessageStream msgStream;
        uint8_t clientId;
    };

    void handleMessage(Connection& client, const void* data, Uint32 len);

    NET_Server* server = nullptr;

    std::vector<Connection> clients;
    EventQueue eventQueue;

    uint8_t nextClientId = 0;
};
