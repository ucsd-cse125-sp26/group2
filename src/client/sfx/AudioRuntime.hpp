/// @file AudioRuntime.hpp
/// @brief Wwise-like data-driven audio runtime definitions and resolver.

#pragma once

#include "AudioMath.hpp"
#include "SfxTypes.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace audio
{

using StableId = std::uint32_t;

struct AudioEventId
{
    StableId value = 0;
    friend bool operator==(AudioEventId, AudioEventId) = default;
};

struct AudioNodeId
{
    StableId value = 0;
    friend bool operator==(AudioNodeId, AudioNodeId) = default;
};

struct AudioClipId
{
    StableId value = 0;
    friend bool operator==(AudioClipId, AudioClipId) = default;
};

struct AudioBusId
{
    StableId value = 0;
    friend bool operator==(AudioBusId, AudioBusId) = default;
};

struct RtpcId
{
    StableId value = 0;
    friend bool operator==(RtpcId, RtpcId) = default;
};

struct SwitchGroupId
{
    StableId value = 0;
    friend bool operator==(SwitchGroupId, SwitchGroupId) = default;
};

struct SwitchValueId
{
    StableId value = 0;
    friend bool operator==(SwitchValueId, SwitchValueId) = default;
};

struct StateGroupId
{
    StableId value = 0;
    friend bool operator==(StateGroupId, StateGroupId) = default;
};

struct StateValueId
{
    StableId value = 0;
    friend bool operator==(StateValueId, StateValueId) = default;
};

struct AudioObjectId
{
    StableId value = 0;
    friend bool operator==(AudioObjectId, AudioObjectId) = default;
};

inline constexpr AudioObjectId kGlobalObject{0};
inline constexpr AudioBusId kInvalidBus{0};

[[nodiscard]] constexpr StableId stableHash(std::string_view text) noexcept
{
    StableId hash = 2166136261u;
    for (char c : text) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

[[nodiscard]] inline AudioEventId eventId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline AudioNodeId nodeId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline AudioClipId clipId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline AudioBusId busId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline RtpcId rtpcId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline SwitchGroupId switchGroupId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline SwitchValueId switchValueId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline StateGroupId stateGroupId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline StateValueId stateValueId(std::string_view name) noexcept
{
    return {stableHash(name)};
}
[[nodiscard]] inline AudioObjectId objectId(std::string_view name) noexcept
{
    return {stableHash(name)};
}

enum class AudioNodeType : std::uint8_t
{
    Sound,
    Random,
    Sequence,
    Switch,
    Blend,
};

enum class AudioActionType : std::uint8_t
{
    Play,
    Stop,
    SetRtpc,
    SetSwitch,
    SetState,
    SetBusVolume,
};

enum class AudioCommandType : std::uint8_t
{
    Play,
    StopClip,
};

struct AudioBusDef
{
    AudioBusId id{};
    AudioBusId parent{};
    std::string name;
    float volume = 1.0f;
    float priorityOffset = 0.0f;
    std::uint16_t maxVoices = 0; // 0 = unlimited.
};

struct AudioClipDef
{
    AudioClipId id{};
    std::string name;
    SfxId sfx = SfxId::_Count;
    AudioBusId bus{};
    float gain = 1.0f;
    float priority = 1.0f;
    float cooldownSeconds = 0.0f;
    float fullGainDistance = k_fullGainDistance;
    float silentDistance = k_silentDistance;
    bool loop = false;
    bool spatial = false;
    std::uint16_t maxInstances = 0; // 0 = unlimited.
};

struct AudioNodeChild
{
    AudioNodeId node{};
    float weight = 1.0f;
    float gain = 1.0f;
    float value = 0.0f; // RTPC/blend point.
    SwitchValueId switchValue{};
};

struct AudioNodeDef
{
    AudioNodeId id{};
    std::string name;
    AudioNodeType type = AudioNodeType::Sound;
    AudioClipId clip{};
    std::vector<AudioNodeChild> children;
    SwitchGroupId switchGroup{};
    StateGroupId stateGroup{};
    RtpcId rtpc{};
    AudioNodeId defaultChild{};
    float gain = 1.0f;
    float priority = 0.0f;
    bool force2D = false;
    bool force3D = false;
    bool loopOverride = false;
    bool hasLoopOverride = false;
};

struct AudioAction
{
    AudioActionType type = AudioActionType::Play;
    AudioNodeId targetNode{};
    AudioClipId targetClip{};
    RtpcId rtpc{};
    SwitchGroupId switchGroup{};
    SwitchValueId switchValue{};
    StateGroupId stateGroup{};
    StateValueId stateValue{};
    AudioBusId bus{};
    float value = 0.0f;
    float gain = 1.0f;
};

struct AudioEventDef
{
    AudioEventId id{};
    std::string name;
    std::vector<AudioAction> actions;
};

struct AudioCommand
{
    AudioCommandType type = AudioCommandType::Play;
    SfxId sfx = SfxId::_Count;
    AudioBusId bus{};
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float gain = 1.0f;
    float priority = 1.0f;
    float cooldownSeconds = 0.0f;
    float fullGainDistance = k_fullGainDistance;
    float silentDistance = k_silentDistance;
    bool loop = false;
    bool positional = false;
    std::uint16_t maxInstances = 0;
    std::uint16_t maxBusInstances = 0;
};

struct AudioObjectState
{
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    std::unordered_map<StableId, float> rtpcs;
    std::unordered_map<StableId, StableId> switches;
};

struct AudioRuntimeStats
{
    std::uint64_t postedEvents = 0;
    std::uint64_t missingEvents = 0;
    std::uint64_t commandsGenerated = 0;
    std::uint64_t nodesVisited = 0;
    std::uint64_t randomChoices = 0;
    std::uint64_t switchFallbacks = 0;
    std::uint64_t blendEvaluations = 0;
    std::uint64_t manifestErrors = 0;
};

class AudioManifest
{
public:
    bool loadFromFile(std::string_view path, std::vector<std::string>* errors = nullptr);
    void buildDefault();

    [[nodiscard]] const AudioEventDef* findEvent(AudioEventId id) const;
    [[nodiscard]] const AudioNodeDef* findNode(AudioNodeId id) const;
    [[nodiscard]] const AudioClipDef* findClip(AudioClipId id) const;
    [[nodiscard]] const AudioBusDef* findBus(AudioBusId id) const;
    [[nodiscard]] AudioBusId resolveBus(std::string_view name) const;
    [[nodiscard]] AudioEventId resolveEvent(std::string_view name) const;
    [[nodiscard]] AudioNodeId resolveNode(std::string_view name) const;
    [[nodiscard]] AudioClipId resolveClip(std::string_view name) const;
    [[nodiscard]] std::span<const AudioBusDef> busses() const noexcept { return busses_; }
    [[nodiscard]] std::span<const AudioClipDef> clips() const noexcept { return clips_; }
    [[nodiscard]] std::span<const AudioEventDef> events() const noexcept { return events_; }
    [[nodiscard]] std::span<const AudioNodeDef> nodes() const noexcept { return nodes_; }

private:
    std::vector<AudioBusDef> busses_;
    std::vector<AudioClipDef> clips_;
    std::vector<AudioNodeDef> nodes_;
    std::vector<AudioEventDef> events_;
    std::unordered_map<StableId, std::size_t> busIndex_;
    std::unordered_map<StableId, std::size_t> clipIndex_;
    std::unordered_map<StableId, std::size_t> nodeIndex_;
    std::unordered_map<StableId, std::size_t> eventIndex_;

    void rebuildIndexes();
};

class AudioRuntime
{
public:
    bool loadManifest(std::string_view path, std::vector<std::string>* errors = nullptr);
    void loadDefaultManifest();

    [[nodiscard]] const AudioManifest& manifest() const noexcept { return manifest_; }
    [[nodiscard]] const AudioRuntimeStats& stats() const noexcept { return stats_; }
    void resetStats() noexcept { stats_ = {}; }

    void
    setObjectTransform(AudioObjectId object, const glm::vec3& position, const glm::vec3& velocity = glm::vec3{0.0f});
    void removeObject(AudioObjectId object);
    void setRtpc(AudioObjectId object, RtpcId rtpc, float value);
    void setSwitch(AudioObjectId object, SwitchGroupId group, SwitchValueId value);
    void setState(StateGroupId group, StateValueId value);
    void setBusVolume(AudioBusId bus, float volume);

    [[nodiscard]] float rtpcValue(AudioObjectId object, RtpcId rtpc, float fallback = 0.0f) const;
    [[nodiscard]] SwitchValueId switchValue(AudioObjectId object, SwitchGroupId group) const;
    [[nodiscard]] float busGain(AudioBusId bus) const;
    [[nodiscard]] std::uint16_t busMaxVoices(AudioBusId bus) const;
    [[nodiscard]] float busPriorityOffset(AudioBusId bus) const;

    [[nodiscard]] std::vector<AudioCommand>
    postEvent(AudioEventId event, AudioObjectId object = kGlobalObject, float gain = 1.0f);
    [[nodiscard]] std::vector<AudioCommand>
    postEvent(std::string_view eventName, AudioObjectId object = kGlobalObject, float gain = 1.0f);

private:
    AudioManifest manifest_;
    std::unordered_map<StableId, AudioObjectState> objects_;
    std::unordered_map<StableId, StableId> states_;
    std::unordered_map<StableId, float> busVolumeOverrides_;
    std::unordered_map<StableId, std::size_t> randomLastChoice_;
    std::unordered_map<StableId, std::size_t> sequenceCursors_;
    mutable AudioRuntimeStats stats_{};
    std::mt19937 rng_{0xA0D10125u};

    void
    resolveNode(const AudioNodeDef& node, AudioObjectId object, float gain, std::vector<AudioCommand>& out, int depth);
    void resolveStopNode(const AudioNodeDef& node, std::vector<AudioCommand>& out, int depth) const;
    void appendClipCommand(const AudioClipDef& clip,
                           AudioObjectId object,
                           float gain,
                           float priorityOffset,
                           bool force2D,
                           bool force3D,
                           std::optional<bool> loopOverride,
                           std::vector<AudioCommand>& out) const;
    [[nodiscard]] const AudioObjectState* findObject(AudioObjectId object) const;
};

} // namespace audio

namespace std
{
template <>
struct hash<audio::AudioEventId>
{
    size_t operator()(audio::AudioEventId id) const noexcept { return id.value; }
};
template <>
struct hash<audio::AudioObjectId>
{
    size_t operator()(audio::AudioObjectId id) const noexcept { return id.value; }
};
} // namespace std
