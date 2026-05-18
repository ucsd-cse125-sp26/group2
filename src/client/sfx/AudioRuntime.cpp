/// @file AudioRuntime.cpp
/// @brief Data-driven Wwise-like audio manifest and runtime resolver.

#include "AudioRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <toml++/toml.hpp>
#include <utility>

namespace audio
{
namespace
{

std::string strOr(const toml::table& table, std::string_view key, std::string fallback = {})
{
    return table[std::string(key)].value<std::string>().value_or(std::move(fallback));
}

float floatOr(const toml::table& table, std::string_view key, float fallback)
{
    return static_cast<float>(table[std::string(key)].value<double>().value_or(static_cast<double>(fallback)));
}

bool boolOr(const toml::table& table, std::string_view key, bool fallback)
{
    return table[std::string(key)].value<bool>().value_or(fallback);
}

std::uint16_t u16Or(const toml::table& table, std::string_view key, std::uint16_t fallback)
{
    const auto value = table[std::string(key)].value<std::int64_t>();
    if (!value)
        return fallback;
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(*value, 0, std::numeric_limits<std::uint16_t>::max()));
}

AudioNodeType parseNodeType(std::string_view type)
{
    if (type == "random")
        return AudioNodeType::Random;
    if (type == "sequence")
        return AudioNodeType::Sequence;
    if (type == "switch")
        return AudioNodeType::Switch;
    if (type == "blend")
        return AudioNodeType::Blend;
    return AudioNodeType::Sound;
}

AudioActionType parseActionType(std::string_view type)
{
    if (type == "stop")
        return AudioActionType::Stop;
    if (type == "set_rtpc")
        return AudioActionType::SetRtpc;
    if (type == "set_switch")
        return AudioActionType::SetSwitch;
    if (type == "set_state")
        return AudioActionType::SetState;
    if (type == "set_bus_volume")
        return AudioActionType::SetBusVolume;
    return AudioActionType::Play;
}

AudioBusDef makeBus(std::string name,
                    std::string_view parent,
                    float volume = 1.0f,
                    std::uint16_t maxVoices = 0,
                    float priorityOffset = 0.0f)
{
    return AudioBusDef{
        .id = busId(name),
        .parent = parent.empty() ? kInvalidBus : busId(parent),
        .name = std::move(name),
        .volume = volume,
        .priorityOffset = priorityOffset,
        .maxVoices = maxVoices,
    };
}

AudioClipDef makeClip(std::string name,
                      SfxId sfx,
                      std::string_view busName,
                      float gain,
                      float priority,
                      float cooldown,
                      bool spatial,
                      bool loop = false,
                      std::uint16_t maxInstances = 0)
{
    return AudioClipDef{
        .id = clipId(name),
        .name = std::move(name),
        .sfx = sfx,
        .bus = busId(busName),
        .gain = gain,
        .priority = priority,
        .cooldownSeconds = cooldown,
        .loop = loop,
        .spatial = spatial,
        .maxInstances = maxInstances,
    };
}

AudioNodeDef makeSoundNode(std::string name, std::string_view clipName, float gain = 1.0f)
{
    return AudioNodeDef{
        .id = nodeId(name),
        .name = std::move(name),
        .type = AudioNodeType::Sound,
        .clip = clipId(clipName),
        .gain = gain,
    };
}

AudioEventDef makePlayEvent(std::string name, std::string_view nodeName, float gain = 1.0f)
{
    AudioEventDef event;
    event.id = eventId(name);
    event.name = std::move(name);
    event.actions.push_back(AudioAction{.type = AudioActionType::Play, .targetNode = nodeId(nodeName), .gain = gain});
    return event;
}

void appendIndex(std::unordered_map<StableId, std::size_t>& index, StableId id, std::size_t value)
{
    if (id != 0)
        index[id] = value;
}

} // namespace

bool AudioManifest::loadFromFile(std::string_view path, std::vector<std::string>* errors)
{
    busses_.clear();
    clips_.clear();
    nodes_.clear();
    events_.clear();
    try {
        toml::table root = toml::parse_file(std::string(path));

        if (auto* busses = root["busses"].as_array()) {
            for (toml::node& node : *busses) {
                const auto* table = node.as_table();
                if (!table)
                    continue;
                const std::string name = strOr(*table, "id");
                if (name.empty())
                    continue;
                busses_.push_back(AudioBusDef{
                    .id = busId(name),
                    .parent = strOr(*table, "parent").empty() ? kInvalidBus : busId(strOr(*table, "parent")),
                    .name = name,
                    .volume = floatOr(*table, "volume", 1.0f),
                    .priorityOffset = floatOr(*table, "priority_offset", 0.0f),
                    .maxVoices = u16Or(*table, "max_voices", 0),
                });
            }
        }

        if (auto* clips = root["clips"].as_array()) {
            for (toml::node& node : *clips) {
                const auto* table = node.as_table();
                if (!table)
                    continue;
                const std::string name = strOr(*table, "id");
                const std::string sfxName = strOr(*table, "sfx");
                const auto sfx = sfxIdFromName(sfxName);
                if (name.empty() || !sfx) {
                    if (errors)
                        errors->push_back("audio manifest clip '" + name + "' has unknown sfx '" + sfxName + "'");
                    continue;
                }
                const std::string busName = strOr(*table, "bus", "SFX");
                clips_.push_back(AudioClipDef{
                    .id = clipId(name),
                    .name = name,
                    .sfx = *sfx,
                    .bus = busId(busName),
                    .gain = floatOr(*table, "gain", 1.0f),
                    .priority = floatOr(*table, "priority", 1.0f),
                    .cooldownSeconds = floatOr(*table, "cooldown", 0.0f),
                    .loop = boolOr(*table, "loop", false),
                    .spatial = boolOr(*table, "spatial", false),
                    .maxInstances = u16Or(*table, "max_instances", 0),
                });
            }
        }

        if (auto* nodes = root["nodes"].as_array()) {
            for (toml::node& node : *nodes) {
                const auto* table = node.as_table();
                if (!table)
                    continue;
                const std::string name = strOr(*table, "id");
                if (name.empty())
                    continue;
                AudioNodeDef def;
                def.id = nodeId(name);
                def.name = name;
                def.type = parseNodeType(strOr(*table, "type", "sound"));
                def.clip = clipId(strOr(*table, "clip"));
                def.switchGroup = switchGroupId(strOr(*table, "switch"));
                def.rtpc = rtpcId(strOr(*table, "rtpc"));
                def.defaultChild = nodeId(strOr(*table, "default"));
                def.gain = floatOr(*table, "gain", 1.0f);
                def.priority = floatOr(*table, "priority", 0.0f);
                def.force2D = boolOr(*table, "force_2d", false);
                def.force3D = boolOr(*table, "force_3d", false);
                if ((*table)["loop"]) {
                    def.hasLoopOverride = true;
                    def.loopOverride = boolOr(*table, "loop", false);
                }
                if (auto* children = (*table)["children"].as_array()) {
                    for (const toml::node& childNode : *children) {
                        const auto* child = childNode.as_table();
                        if (!child)
                            continue;
                        const std::string childId = strOr(*child, "node");
                        if (childId.empty())
                            continue;
                        def.children.push_back(AudioNodeChild{
                            .node = nodeId(childId),
                            .weight = floatOr(*child, "weight", 1.0f),
                            .gain = floatOr(*child, "gain", 1.0f),
                            .value = floatOr(*child, "value", 0.0f),
                            .switchValue = switchValueId(strOr(*child, "switch")),
                        });
                    }
                }
                nodes_.push_back(std::move(def));
            }
        }

        if (auto* events = root["events"].as_array()) {
            for (toml::node& node : *events) {
                const auto* table = node.as_table();
                if (!table)
                    continue;
                const std::string name = strOr(*table, "id");
                if (name.empty())
                    continue;
                AudioEventDef event;
                event.id = eventId(name);
                event.name = name;
                if (auto* actions = (*table)["actions"].as_array()) {
                    for (const toml::node& actionNode : *actions) {
                        const auto* actionTable = actionNode.as_table();
                        if (!actionTable)
                            continue;
                        const std::string target = strOr(*actionTable, "target");
                        const std::string busName = strOr(*actionTable, "bus");
                        const std::string switchName = strOr(*actionTable, "switch");
                        const std::string valueName = strOr(*actionTable, "value_name");
                        event.actions.push_back(AudioAction{
                            .type = parseActionType(strOr(*actionTable, "type", "play")),
                            .targetNode = nodeId(target),
                            .targetClip = clipId(target),
                            .rtpc = rtpcId(strOr(*actionTable, "rtpc")),
                            .switchGroup = switchGroupId(switchName),
                            .switchValue = switchValueId(valueName),
                            .stateGroup = stateGroupId(strOr(*actionTable, "state")),
                            .stateValue = stateValueId(valueName),
                            .bus = busName.empty() ? kInvalidBus : busId(busName),
                            .value = floatOr(*actionTable, "value", 0.0f),
                            .gain = floatOr(*actionTable, "gain", 1.0f),
                        });
                    }
                }
                events_.push_back(std::move(event));
            }
        }
    } catch (const toml::parse_error& e) {
        if (errors)
            errors->push_back(std::string("audio manifest parse failed: ") + e.what());
        return false;
    }

    rebuildIndexes();
    if (events_.empty() || nodes_.empty() || clips_.empty()) {
        if (errors)
            errors->push_back("audio manifest missing clips, nodes, or events");
        return false;
    }
    return true;
}

void AudioManifest::buildDefault()
{
    busses_ = {
        makeBus("Master", ""),
        makeBus("SFX", "Master", 1.0f, 48),
        makeBus("Weapons", "SFX", 1.0f, 18, 0.3f),
        makeBus("Impacts", "SFX", 0.95f, 16, 0.0f),
        makeBus("Player", "SFX", 1.0f, 12, 0.6f),
        makeBus("Footsteps", "SFX", 0.75f, 20, -0.4f),
        makeBus("VoiceChat", "Master", 1.0f, 12, 0.8f),
        makeBus("UI", "Master", 1.0f, 10, 0.2f),
    };

    clips_ = {
        makeClip("rifle_fire", SfxId::RifleFire, "Weapons", 1.0f, 2.0f, 0.10f, true, false, 10),
        makeClip("rocket_fire", SfxId::RocketFire, "Weapons", 1.0f, 2.2f, 0.80f, true, false, 4),
        makeClip("railgun_fire", SfxId::ChargeRifleShoot, "Weapons", 1.0f, 2.5f, 0.20f, true, false, 4),
        makeClip("railgun_charge_start", SfxId::ChargeRifleLoad, "Weapons", 0.9f, 1.7f, 0.0f, false, false, 1),
        makeClip("energy_fire", SfxId::EnergyGunFire, "Weapons", 0.9f, 1.8f, 0.08f, true, false, 8),
        makeClip("grenade_throw", SfxId::GrenadeThrow, "Weapons", 0.9f, 1.2f, 0.12f, true, false, 8),
        makeClip("beam_loop", SfxId::EnergyBeamLoop, "Weapons", 0.55f, 1.8f, 0.0f, false, true, 2),
        makeClip("flesh_hit", SfxId::FleshHit, "Impacts", 0.75f, 1.6f, 0.08f, true, false, 8),
        makeClip("headshot", SfxId::Headshot, "Impacts", 0.9f, 2.0f, 0.08f, false, false, 3),
        makeClip("world_impact", SfxId::FootstepLight, "Impacts", 0.35f, 0.7f, 0.04f, true, false, 12),
        makeClip("explosion", SfxId::Explosion, "Impacts", 1.0f, 3.0f, 0.30f, true, false, 4),
        makeClip("damage_taken", SfxId::DamageTaken, "Player", 0.9f, 3.0f, 0.30f, false, false, 2),
        makeClip("armor_break", SfxId::ArmorBreak, "Player", 1.0f, 3.2f, 1.0f, false, false, 2),
        makeClip("death", SfxId::Death, "Player", 1.0f, 3.0f, 2.0f, false, false, 1),
        makeClip("respawn", SfxId::Respawn, "Player", 0.8f, 2.4f, 2.0f, false, false, 1),
        makeClip("kill_confirm", SfxId::KillConfirm, "Player", 0.9f, 3.0f, 0.30f, false, false, 3),
        makeClip("healing", SfxId::Healing, "Player", 0.55f, 1.2f, 1.0f, false, false, 1),
        makeClip("footstep_light", SfxId::FootstepLight, "Footsteps", 0.55f, 0.8f, 0.06f, true, false, 16),
        makeClip("footstep_heavy", SfxId::FootstepHeavy, "Footsteps", 0.75f, 0.9f, 0.06f, true, false, 16),
        makeClip("voice_start", SfxId::VoiceStart, "VoiceChat", 0.2f, 1.0f, 0.05f, false, false, 2),
        makeClip("voice_stop", SfxId::VoiceStop, "VoiceChat", 0.14f, 1.0f, 0.05f, false, false, 2),
    };

    nodes_.clear();
    for (const auto& clip : clips_)
        nodes_.push_back(makeSoundNode("node." + clip.name, clip.name));

    AudioNodeDef footstepSwitch;
    footstepSwitch.id = nodeId("node.footstep.by_weight");
    footstepSwitch.name = "node.footstep.by_weight";
    footstepSwitch.type = AudioNodeType::Blend;
    footstepSwitch.rtpc = rtpcId("movement.intensity");
    footstepSwitch.children = {
        {.node = nodeId("node.footstep_light"), .gain = 1.0f, .value = 0.0f},
        {.node = nodeId("node.footstep_heavy"), .gain = 1.0f, .value = 1.0f},
    };
    nodes_.push_back(std::move(footstepSwitch));

    events_ = {
        makePlayEvent("weapon.rifle.fire", "node.rifle_fire"),
        makePlayEvent("weapon.rocket.fire", "node.rocket_fire"),
        makePlayEvent("weapon.railgun.fire", "node.railgun_fire"),
        makePlayEvent("weapon.railgun.charge_start", "node.railgun_charge_start"),
        makePlayEvent("weapon.energy.fire", "node.energy_fire"),
        makePlayEvent("weapon.grenade.throw", "node.grenade_throw"),
        makePlayEvent("weapon.energy.loop", "node.beam_loop"),
        makePlayEvent("impact.flesh", "node.flesh_hit"),
        makePlayEvent("impact.headshot", "node.headshot"),
        makePlayEvent("impact.world", "node.world_impact"),
        makePlayEvent("explosion", "node.explosion"),
        makePlayEvent("player.damage", "node.damage_taken"),
        makePlayEvent("player.armor_break", "node.armor_break"),
        makePlayEvent("player.death", "node.death"),
        makePlayEvent("player.respawn", "node.respawn"),
        makePlayEvent("player.kill_confirm", "node.kill_confirm"),
        makePlayEvent("player.healing", "node.healing"),
        makePlayEvent("footstep", "node.footstep.by_weight"),
        makePlayEvent("footstep.light", "node.footstep_light"),
        makePlayEvent("footstep.heavy", "node.footstep_heavy"),
        makePlayEvent("voice.start", "node.voice_start"),
        makePlayEvent("voice.stop", "node.voice_stop"),
    };

    rebuildIndexes();
}

const AudioEventDef* AudioManifest::findEvent(AudioEventId id) const
{
    const auto it = eventIndex_.find(id.value);
    return it == eventIndex_.end() ? nullptr : &events_[it->second];
}

const AudioNodeDef* AudioManifest::findNode(AudioNodeId id) const
{
    const auto it = nodeIndex_.find(id.value);
    return it == nodeIndex_.end() ? nullptr : &nodes_[it->second];
}

const AudioClipDef* AudioManifest::findClip(AudioClipId id) const
{
    const auto it = clipIndex_.find(id.value);
    return it == clipIndex_.end() ? nullptr : &clips_[it->second];
}

const AudioBusDef* AudioManifest::findBus(AudioBusId id) const
{
    const auto it = busIndex_.find(id.value);
    return it == busIndex_.end() ? nullptr : &busses_[it->second];
}

AudioBusId AudioManifest::resolveBus(std::string_view name) const
{
    const AudioBusId id = busId(name);
    return findBus(id) ? id : kInvalidBus;
}

AudioEventId AudioManifest::resolveEvent(std::string_view name) const
{
    const AudioEventId id = eventId(name);
    return findEvent(id) ? id : AudioEventId{};
}

AudioNodeId AudioManifest::resolveNode(std::string_view name) const
{
    const AudioNodeId id = nodeId(name);
    return findNode(id) ? id : AudioNodeId{};
}

AudioClipId AudioManifest::resolveClip(std::string_view name) const
{
    const AudioClipId id = clipId(name);
    return findClip(id) ? id : AudioClipId{};
}

void AudioManifest::rebuildIndexes()
{
    busIndex_.clear();
    clipIndex_.clear();
    nodeIndex_.clear();
    eventIndex_.clear();
    for (std::size_t i = 0; i < busses_.size(); ++i)
        appendIndex(busIndex_, busses_[i].id.value, i);
    for (std::size_t i = 0; i < clips_.size(); ++i)
        appendIndex(clipIndex_, clips_[i].id.value, i);
    for (std::size_t i = 0; i < nodes_.size(); ++i)
        appendIndex(nodeIndex_, nodes_[i].id.value, i);
    for (std::size_t i = 0; i < events_.size(); ++i)
        appendIndex(eventIndex_, events_[i].id.value, i);
}

bool AudioRuntime::loadManifest(std::string_view path, std::vector<std::string>* errors)
{
    AudioManifest loaded;
    if (!loaded.loadFromFile(path, errors)) {
        ++stats_.manifestErrors;
        return false;
    }
    manifest_ = std::move(loaded);
    objects_.clear();
    states_.clear();
    busVolumeOverrides_.clear();
    randomLastChoice_.clear();
    sequenceCursors_.clear();
    return true;
}

void AudioRuntime::loadDefaultManifest()
{
    manifest_.buildDefault();
    objects_.clear();
    states_.clear();
    busVolumeOverrides_.clear();
    randomLastChoice_.clear();
    sequenceCursors_.clear();
}

void AudioRuntime::setObjectTransform(AudioObjectId object, const glm::vec3& position, const glm::vec3& velocity)
{
    if (object == kGlobalObject)
        return;
    AudioObjectState& state = objects_[object.value];
    state.position = position;
    state.velocity = velocity;
}

void AudioRuntime::removeObject(AudioObjectId object)
{
    objects_.erase(object.value);
}

void AudioRuntime::setRtpc(AudioObjectId object, RtpcId rtpc, float value)
{
    if (rtpc.value == 0)
        return;
    objects_[object.value].rtpcs[rtpc.value] = value;
}

void AudioRuntime::setSwitch(AudioObjectId object, SwitchGroupId group, SwitchValueId value)
{
    if (group.value == 0)
        return;
    objects_[object.value].switches[group.value] = value.value;
}

void AudioRuntime::setState(StateGroupId group, StateValueId value)
{
    if (group.value != 0)
        states_[group.value] = value.value;
}

void AudioRuntime::setBusVolume(AudioBusId bus, float volume)
{
    if (bus.value != 0)
        busVolumeOverrides_[bus.value] = std::clamp(volume, 0.0f, 4.0f);
}

float AudioRuntime::rtpcValue(AudioObjectId object, RtpcId rtpc, float fallback) const
{
    if (rtpc.value == 0)
        return fallback;
    if (const AudioObjectState* obj = findObject(object)) {
        const auto it = obj->rtpcs.find(rtpc.value);
        if (it != obj->rtpcs.end())
            return it->second;
    }
    if (const AudioObjectState* global = findObject(kGlobalObject)) {
        const auto it = global->rtpcs.find(rtpc.value);
        if (it != global->rtpcs.end())
            return it->second;
    }
    return fallback;
}

SwitchValueId AudioRuntime::switchValue(AudioObjectId object, SwitchGroupId group) const
{
    if (group.value == 0)
        return {};
    if (const AudioObjectState* obj = findObject(object)) {
        const auto it = obj->switches.find(group.value);
        if (it != obj->switches.end())
            return {it->second};
    }
    if (const AudioObjectState* global = findObject(kGlobalObject)) {
        const auto it = global->switches.find(group.value);
        if (it != global->switches.end())
            return {it->second};
    }
    return {};
}

float AudioRuntime::busGain(AudioBusId bus) const
{
    if (bus.value == 0)
        return 1.0f;
    float gain = 1.0f;
    AudioBusId current = bus;
    for (int depth = 0; depth < 16 && current.value != 0; ++depth) {
        const AudioBusDef* def = manifest_.findBus(current);
        if (!def)
            break;
        const auto overrideIt = busVolumeOverrides_.find(current.value);
        gain *= overrideIt == busVolumeOverrides_.end() ? def->volume : overrideIt->second;
        current = def->parent;
    }
    return gain;
}

std::uint16_t AudioRuntime::busMaxVoices(AudioBusId bus) const
{
    const AudioBusDef* def = manifest_.findBus(bus);
    return def ? def->maxVoices : 0;
}

float AudioRuntime::busPriorityOffset(AudioBusId bus) const
{
    float offset = 0.0f;
    AudioBusId current = bus;
    for (int depth = 0; depth < 16 && current.value != 0; ++depth) {
        const AudioBusDef* def = manifest_.findBus(current);
        if (!def)
            break;
        offset += def->priorityOffset;
        current = def->parent;
    }
    return offset;
}

std::vector<AudioCommand> AudioRuntime::postEvent(AudioEventId event, AudioObjectId object, float gain)
{
    ++stats_.postedEvents;
    std::vector<AudioCommand> out;
    const AudioEventDef* def = manifest_.findEvent(event);
    if (!def) {
        ++stats_.missingEvents;
        return out;
    }

    for (const AudioAction& action : def->actions) {
        switch (action.type) {
        case AudioActionType::Play:
            if (const AudioNodeDef* node = manifest_.findNode(action.targetNode))
                resolveNode(*node, object, gain * action.gain, out, 0);
            break;
        case AudioActionType::Stop:
            if (const AudioNodeDef* node = manifest_.findNode(action.targetNode))
                resolveStopNode(*node, out, 0);
            else if (const AudioClipDef* clip = manifest_.findClip(action.targetClip))
                out.push_back(AudioCommand{.type = AudioCommandType::StopClip, .sfx = clip->sfx});
            break;
        case AudioActionType::SetRtpc:
            setRtpc(object, action.rtpc, action.value);
            break;
        case AudioActionType::SetSwitch:
            setSwitch(object, action.switchGroup, action.switchValue);
            break;
        case AudioActionType::SetState:
            setState(action.stateGroup, action.stateValue);
            break;
        case AudioActionType::SetBusVolume:
            setBusVolume(action.bus, action.value);
            break;
        }
    }
    stats_.commandsGenerated += out.size();
    return out;
}

std::vector<AudioCommand> AudioRuntime::postEvent(std::string_view eventName, AudioObjectId object, float gain)
{
    return postEvent(eventId(eventName), object, gain);
}

void AudioRuntime::resolveNode(
    const AudioNodeDef& node, AudioObjectId object, float gain, std::vector<AudioCommand>& out, int depth)
{
    if (depth > 12)
        return;
    ++stats_.nodesVisited;

    switch (node.type) {
    case AudioNodeType::Sound: {
        if (const AudioClipDef* clip = manifest_.findClip(node.clip))
            appendClipCommand(*clip,
                              object,
                              gain * node.gain,
                              node.priority,
                              node.force2D,
                              node.force3D,
                              node.hasLoopOverride ? std::optional<bool>(node.loopOverride) : std::nullopt,
                              out);
        break;
    }
    case AudioNodeType::Random: {
        if (node.children.empty())
            break;
        float total = 0.0f;
        for (const AudioNodeChild& child : node.children)
            total += std::max(0.0f, child.weight);
        if (total <= 0.0f)
            break;
        std::uniform_real_distribution<float> dist(0.0f, total);
        float pick = dist(rng_);
        std::size_t chosen = 0;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            pick -= std::max(0.0f, node.children[i].weight);
            if (pick <= 0.0f) {
                chosen = i;
                break;
            }
        }
        if (node.children.size() > 1) {
            const auto last = randomLastChoice_.find(node.id.value);
            if (last != randomLastChoice_.end() && last->second == chosen)
                chosen = (chosen + 1u) % node.children.size();
            randomLastChoice_[node.id.value] = chosen;
        }
        ++stats_.randomChoices;
        const AudioNodeChild& child = node.children[chosen];
        if (const AudioNodeDef* childNode = manifest_.findNode(child.node))
            resolveNode(*childNode, object, gain * node.gain * child.gain, out, depth + 1);
        break;
    }
    case AudioNodeType::Sequence: {
        if (node.children.empty())
            break;
        std::size_t& cursor = sequenceCursors_[node.id.value];
        const AudioNodeChild& child = node.children[cursor % node.children.size()];
        cursor = (cursor + 1u) % node.children.size();
        if (const AudioNodeDef* childNode = manifest_.findNode(child.node))
            resolveNode(*childNode, object, gain * node.gain * child.gain, out, depth + 1);
        break;
    }
    case AudioNodeType::Switch: {
        const SwitchValueId value = switchValue(object, node.switchGroup);
        const AudioNodeChild* chosen = nullptr;
        for (const AudioNodeChild& child : node.children) {
            if (child.switchValue.value != 0 && child.switchValue == value) {
                chosen = &child;
                break;
            }
        }
        if (!chosen && node.defaultChild.value != 0) {
            ++stats_.switchFallbacks;
            if (const AudioNodeDef* childNode = manifest_.findNode(node.defaultChild)) {
                resolveNode(*childNode, object, gain * node.gain, out, depth + 1);
                break;
            }
        }
        if (chosen) {
            if (const AudioNodeDef* childNode = manifest_.findNode(chosen->node))
                resolveNode(*childNode, object, gain * node.gain * chosen->gain, out, depth + 1);
        }
        break;
    }
    case AudioNodeType::Blend: {
        if (node.children.empty())
            break;
        ++stats_.blendEvaluations;
        const float value = rtpcValue(object, node.rtpc, 0.0f);
        std::vector<const AudioNodeChild*> sorted;
        sorted.reserve(node.children.size());
        for (const auto& child : node.children)
            sorted.push_back(&child);
        std::sort(sorted.begin(), sorted.end(), [](const AudioNodeChild* a, const AudioNodeChild* b) {
            return a->value < b->value;
        });
        if (value <= sorted.front()->value) {
            if (const AudioNodeDef* childNode = manifest_.findNode(sorted.front()->node))
                resolveNode(*childNode, object, gain * node.gain * sorted.front()->gain, out, depth + 1);
            break;
        }
        if (value >= sorted.back()->value) {
            if (const AudioNodeDef* childNode = manifest_.findNode(sorted.back()->node))
                resolveNode(*childNode, object, gain * node.gain * sorted.back()->gain, out, depth + 1);
            break;
        }
        for (std::size_t i = 1; i < sorted.size(); ++i) {
            if (value > sorted[i]->value)
                continue;
            const AudioNodeChild* lo = sorted[i - 1u];
            const AudioNodeChild* hi = sorted[i];
            const float span = std::max(0.001f, hi->value - lo->value);
            const float t = std::clamp((value - lo->value) / span, 0.0f, 1.0f);
            if (const AudioNodeDef* loNode = manifest_.findNode(lo->node))
                resolveNode(*loNode, object, gain * node.gain * lo->gain * (1.0f - t), out, depth + 1);
            if (const AudioNodeDef* hiNode = manifest_.findNode(hi->node))
                resolveNode(*hiNode, object, gain * node.gain * hi->gain * t, out, depth + 1);
            break;
        }
        break;
    }
    }
}

void AudioRuntime::resolveStopNode(const AudioNodeDef& node, std::vector<AudioCommand>& out, int depth) const
{
    if (depth > 12)
        return;
    if (node.type == AudioNodeType::Sound) {
        if (const AudioClipDef* clip = manifest_.findClip(node.clip))
            out.push_back(AudioCommand{.type = AudioCommandType::StopClip, .sfx = clip->sfx});
        return;
    }
    for (const AudioNodeChild& child : node.children) {
        if (const AudioNodeDef* childNode = manifest_.findNode(child.node))
            resolveStopNode(*childNode, out, depth + 1);
    }
    if (node.defaultChild.value != 0) {
        if (const AudioNodeDef* childNode = manifest_.findNode(node.defaultChild))
            resolveStopNode(*childNode, out, depth + 1);
    }
}

void AudioRuntime::appendClipCommand(const AudioClipDef& clip,
                                     AudioObjectId object,
                                     float gain,
                                     float priorityOffset,
                                     bool force2D,
                                     bool force3D,
                                     std::optional<bool> loopOverride,
                                     std::vector<AudioCommand>& out) const
{
    if (clip.sfx == SfxId::_Count || gain <= 0.0001f)
        return;
    const AudioObjectState* state = findObject(object);
    const bool positional = force3D || (clip.spatial && !force2D && object != kGlobalObject);
    out.push_back(AudioCommand{
        .type = AudioCommandType::Play,
        .sfx = clip.sfx,
        .bus = clip.bus,
        .position = state ? state->position : glm::vec3{0.0f},
        .velocity = state ? state->velocity : glm::vec3{0.0f},
        .gain = gain * clip.gain,
        .priority = clip.priority + priorityOffset + busPriorityOffset(clip.bus),
        .cooldownSeconds = clip.cooldownSeconds,
        .loop = loopOverride.value_or(clip.loop),
        .positional = positional,
        .maxInstances = clip.maxInstances,
        .maxBusInstances = busMaxVoices(clip.bus),
    });
}

const AudioObjectState* AudioRuntime::findObject(AudioObjectId object) const
{
    const auto it = objects_.find(object.value);
    return it == objects_.end() ? nullptr : &it->second;
}

} // namespace audio
