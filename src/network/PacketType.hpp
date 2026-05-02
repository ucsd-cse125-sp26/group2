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

    /// @brief Server -> Client: delta against a previously-received full snapshot.
    ///
    /// PR-10 (server-perf): wire format of the payload is
    ///   `[currentTick:u32] [fromTick:u32] [baselineSize:u32] [rleDelta:bytes]`
    /// where `rleDelta` is an RLE stream of (skipBytes:u32, copyBytes:u32,
    /// copyData:u8[copyBytes]) triples — apply over a copy of the baseline
    /// at `fromTick` to reconstruct the full snapshot bytes for tick
    /// `currentTick`. Clients drop the packet if they don't currently have
    /// the baseline; a periodic UPDATE_REGISTRY (full keyframe) recovers.
    UPDATE_REGISTRY_DELTA,

    /// @brief Server -> single shooter: lag-comp shot debug snapshot.
    ///
    /// PR-20: CSGO sv_showimpacts-style debug visualizer.  Sent only
    /// to the shooter client (not broadcast) immediately after the
    /// server resolves a hitscan shot.  Carries the rewound state the
    /// server saw when it processed the shot — origin + direction +
    /// hit point + per-target rewound capsule list — so the client
    /// can overlay "what server hit" (red) on top of "what I aimed at"
    /// (blue) and visually inspect lag-comp alignment.  Wire format
    /// defined in `network/ShotDebugReport.hpp`.
    SHOT_DEBUG_REPORT,
};
