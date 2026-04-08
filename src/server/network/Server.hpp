#pragma once

#include <SDL3/SDL_stdinc.h>

#include <SDL3_net/SDL_net.h>

// ---------------------------------------------------------------------------
// Server — UDP datagram socket wrapper.
//
// Call poll() every tick to drain incoming datagrams and echo them back.
// Extend with proper packet dispatch as the game protocol grows.
// ---------------------------------------------------------------------------

class Server
{
public:
    bool init(const char* addr, Uint16 port);
    void shutdown();

    // Drain all pending datagrams for this tick.
    void poll();

private:
    void handleDatagram(NET_Datagram* dgram);

    NET_DatagramSocket* sock = nullptr;
};
