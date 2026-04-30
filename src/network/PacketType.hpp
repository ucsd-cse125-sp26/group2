/// @file PacketType.hpp
/// @brief Network packet type identifiers for client-server communication.

#pragma once

#include <cstdint>

/// @brief Identifies the type of a network packet.
enum class PacketType : uint8_t
{
    INPUT,            ///< Client -> Server: player input snapshot.
    ASSIGN_CLIENT_ID, ///< Server -> Client: assign the connecting client its entity ID.
    UPDATE_REGISTRY,  ///< Server -> Client: full ECS registry state snapshot.
    PARTICLE_SPAWN,   ///< Server -> All clients: replicated particle/VFX event.
    PING,             ///< Client -> Server: latency measurement (carries uint64_t timestamp).
    PONG,             ///< Server -> Client: latency measurement reply (echoes timestamp).
    MATCH_STATE,      ///< Server -> All clients: match phase transition update.
    KILL_EVENT,       ///< Server -> All clients: player kill notification.
};
