#pragma once

#include <cstdint>

enum class PacketType : uint8_t
{
    // Client -> Server
    INPUT,

    // Server -> Client
    ASSIGN_CLIENT_ID,
    UPDATE_REGISTRY,
};
