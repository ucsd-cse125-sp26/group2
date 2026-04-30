/// @file EventType.hpp
/// @brief Server-side gameplay event type identifiers.

#pragma once

/// @brief Enumeration of gameplay event types for server-side queueing.
enum class EventType
{
    Connected,    ///< A new client has connected.
    Disconnected, ///< A client has disconnected.
    Input,        ///< A client has sent an input snapshot.
};