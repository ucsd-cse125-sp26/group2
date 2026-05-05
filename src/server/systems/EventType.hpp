/// @file EventType.hpp
/// @brief Server-side gameplay event type identifiers.

#pragma once

/// @brief Enumeration of gameplay event types for server-side queueing.
enum class EventType
{
    Connected,    ///< A new client has connected.
    Disconnected, ///< A client has disconnected.
    Input,        ///< A client has sent an input snapshot.

    /// @brief PR-27: client has reported its view of the target's
    /// animation state for a single shot (rising-edge of `shooting`).
    /// The server pairs this with the shooter's INPUT for the same
    /// `shotInputTick` and computes anim-state delta vs its own
    /// historical snapshot at the rewound tick.
    ShotIntent,
};