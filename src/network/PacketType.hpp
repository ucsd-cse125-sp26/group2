#pragma once

#include <cstdint>

enum class PacketType : uint8_t
{
    // Client -> Server
    INPUT,

    // Server -> Client
    ASSIGN_CLIENT_ID,
    UPDATE_REGISTRY,

    // Server -> All clients (particle effect replication)
    PARTICLE_SPAWN,

    // Bidirectional (latency measurement)
    PING, // Client -> Server (carries uint64_t timestamp)
    PONG, // Server -> Client (echoes timestamp back)

    // Server -> All clients (match status updates)
    MATCH_STATE,
};
