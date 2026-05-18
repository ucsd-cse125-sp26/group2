/// @file VoiceChatSystem.cpp
/// @brief Client-side voice capture, decode, jitter, and spatial playback.

#include "VoiceChatSystem.hpp"

#include "client/network/Client.hpp"
#include "client/sfx/SfxSystem.hpp"
#include "ecs/components/Position.hpp"
#include "ecs/components/Velocity.hpp"
#include "ecs/registry/Registry.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>

bool VoiceChatSystem::init()
{
    const bool ok = capture_.init();
    if (!ok)
        SDL_Log("[voice] Capture disabled; text chat and receive-side voice still work.");
    return true;
}

void VoiceChatSystem::quit()
{
    capture_.quit();
    speakers_.clear();
    speaking_.clear();
    std::lock_guard lock(pendingMutex_);
    pendingFrames_.clear();
}

void VoiceChatSystem::setPushToTalk(bool active)
{
    capture_.setPushToTalk(active);
}

void VoiceChatSystem::enqueueFrame(const net::voice::ServerVoiceFrame& frame)
{
    std::lock_guard lock(pendingMutex_);
    if (pendingFrames_.size() >= 128)
        pendingFrames_.erase(pendingFrames_.begin());
    pendingFrames_.push_back(frame);
}

void VoiceChatSystem::update(float dt, Client& client, const Registry& registry, SfxSystem& sfx)
{
    for (const auto& frame : capture_.poll())
        client.sendVoiceFrame(frame.sequence, frame.frameMs, frame.opus);

    std::vector<net::voice::ServerVoiceFrame> incoming;
    {
        std::lock_guard lock(pendingMutex_);
        incoming.swap(pendingFrames_);
    }
    for (const auto& frame : incoming) {
        RemoteSpeaker& remote = speakers_[frame.speaker.value];
        if (!remote.decoderReady)
            remote.decoderReady = remote.decoder.init();
        remote.jitter.push(frame.sequence, frame.frameMs, frame.opus);
        remote.speakingTimer = 0.35f;
    }

    speaking_.clear();
    for (auto it = speakers_.begin(); it != speakers_.end();) {
        RemoteSpeaker& remote = it->second;
        const ClientId speaker{it->first};
        drainSpeaker(speaker, remote, registry, sfx);
        remote.speakingTimer = std::max(0.0f, remote.speakingTimer - dt);
        if (remote.speakingTimer > 0.0f)
            speaking_.push_back({speaker, remote.speakingTimer});
        if (remote.speakingTimer <= 0.0f && remote.jitter.size() == 0)
            it = speakers_.erase(it);
        else
            ++it;
    }
}

bool VoiceChatSystem::lookupSpeakerTransform(const Registry& registry,
                                             ClientId speaker,
                                             glm::vec3& position,
                                             glm::vec3& velocity) const
{
    for (const auto entity : registry.view<const ClientId, const Position>()) {
        if (registry.get<const ClientId>(entity) != speaker)
            continue;
        position = registry.get<const Position>(entity).value;
        if (const auto* vel = registry.try_get<const Velocity>(entity))
            velocity = vel->value;
        else
            velocity = glm::vec3{0.0f};
        return true;
    }
    return false;
}

void VoiceChatSystem::drainSpeaker(ClientId speaker, RemoteSpeaker& remote, const Registry& registry, SfxSystem& sfx)
{
    if (!remote.decoderReady)
        return;

    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    if (!lookupSpeakerTransform(registry, speaker, position, velocity))
        return;

    int drained = 0;
    while (drained < 3) {
        auto next = remote.jitter.pop();
        if (!next)
            break;
        std::vector<float> pcm =
            next->lost ? remote.decoder.conceal(next->frameMs) : remote.decoder.decode(next->opus, next->frameMs);
        if (!pcm.empty())
            sfx.submitVoiceFrame(speaker, next->sequence, pcm, position, velocity);
        ++drained;
    }
}
