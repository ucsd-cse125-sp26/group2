/// @file EventType.hpp
/// @brief Server-side gameplay event type identifiers.

#pragma once

/// @brief Enumeration of gameplay event types for server-side queueing.
enum class EventType
{
    Connected,           ///< A new client has connected.
    Disconnected,        ///< A client has disconnected.
    Input,               ///< A client has sent an input snapshot.
    PlayerReady,         ///< A client has marked themselves ready in the lobby.
    PlayerUnready,       ///< A client has marked themselves unready in the lobby.
    StartMatchRequested, ///< A client has requested a host-started match transition.

    /// @brief PR-27: client has reported its view of the target's
    /// animation state for a single shot (rising-edge of `shooting`).
    /// The server pairs this with the shooter's INPUT for the same
    /// `shotInputTick` and computes anim-state delta vs its own
    /// historical snapshot at the rewound tick.
    ShotIntent,

    TextChat,                 ///< Client submitted a bounded all-chat message.
    VoiceFrame,               ///< Client submitted one Opus voice frame for proximity routing.
    PhysicsDiagRecording,     ///< Client toggled authoritative physics CSV recording.

    MatchConfigUpdated,       ///< Client proposed a new match config (e.g. kill threshold).
    DiscoverySettingsUpdated, ///< Client proposed new discovery advertisement settings.
    ServerShutdownRequested,  ///< Client requested server shutdown.
    GameplayReady,            ///< Client finished initializing game
};
