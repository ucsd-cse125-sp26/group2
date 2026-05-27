/// @file Event.hpp
/// @brief Client Event structure to be consumed by server game loop.
#pragma once
#include "EventType.hpp"
#include "ecs/components/AnimSnapshot.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "network/MatchConfig.hpp"

#include <cstdint>
#include <string>
#include <vector>

/// @brief PR-27: per-shot animation-state assertion from the client.
///
/// Carries the client's view of the target's animation state at the
/// instant the user pulled the trigger, plus the input tick that the
/// shot is associated with.  Server pairs this with the shooter's
/// INPUT for the same `shotInputTick` and compares against its own
/// historical anim state at the rewound tick.  When `targetClientId
/// == 0xFFFF` the client wasn't aiming at anyone close — the row is
/// still emitted (as a "no target" telemetry row) but no comparison
/// happens.
struct ShotIntentPayload
{
    std::uint32_t shotInputTick = 0;
    std::uint16_t targetClientId = 0xFFFFu; ///< 0xFFFF = no specific target.
    AnimSnapshot targetAnim{};
};

struct TextChatPayload
{
    std::uint16_t clientSeq = 0;
    std::string message;
};

struct VoiceFramePayload
{
    std::uint16_t sequence = 0;
    std::uint8_t frameMs = 20;
    std::vector<std::uint8_t> opus;
};

/// @brief A single gameplay event produced by network input processing.
class Event
{
public:
    ClientId clientId;                 ///< Originating client identifier.
    EventType type;
    InputSnapshot movementIntent = {}; ///< Used when `type == Input`.
    ShotIntentPayload shotIntent = {}; ///< Used when `type == ShotIntent` (PR-27).
    TextChatPayload textChat = {};     ///< Used when `type == TextChat`.
    VoiceFramePayload voiceFrame = {}; ///< Used when `type == VoiceFrame`.
    MatchConfig matchConfig = {};      ///< Used when `type == MatchConfigUpdated`.
    bool physicsDiagRecording = false; ///< Used when `type == PhysicsDiagRecording`.
};
