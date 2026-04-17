#pragma once

#include <cstdint>

enum class PacketType : uint8_t
{
    // Client -> Server
    INPUT,

    // Server -> Client
    ASSIGN_CLIENT_ID,
    UPDATE_REGISTRY,

    // Bidirectional (latency measurement)
    PING, // Client -> Server (carries uint64_t timestamp)
    PONG, // Server -> Client (echoes timestamp back)
};
