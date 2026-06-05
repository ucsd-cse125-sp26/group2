/// @file VoiceChatSystem.hpp
/// @brief Client-side push-to-talk voice capture, decode, jitter, and spatial playback.

#pragma once

#include "VoiceCapture.hpp"
#include "VoiceCodec.hpp"
#include "VoiceJitterBuffer.hpp"
#include "ecs/components/ClientId.hpp"
#include "ecs/registry/Registry.hpp"
#include "network/VoiceProtocol.hpp"

#include <glm/glm.hpp>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

class Client;
class SfxSystem;

class VoiceChatSystem
{
public:
    struct SpeakingState
    {
        ClientId speaker{};
        float remainingSeconds = 0.0f;
    };

    bool init(std::string_view recordingDeviceName = {});
    void quit();
    void setRecordingDeviceName(std::string_view name);
    void setPushToTalk(bool active);
    void enqueueFrame(const net::voice::ServerVoiceFrame& frame);
    void update(float dt, Client& client, const Registry& registry, SfxSystem& sfx);
    [[nodiscard]] bool captureReady() const noexcept { return capture_.ready(); }
    [[nodiscard]] bool transmitting() const noexcept { return capture_.transmitting(); }
    [[nodiscard]] const std::vector<SpeakingState>& speaking() const noexcept { return speaking_; }

private:
    struct RemoteSpeaker
    {
        VoiceDecoder decoder;
        VoiceJitterBuffer jitter{2, 10};
        float speakingTimer = 0.0f;
        bool decoderReady = false;
    };

    [[nodiscard]] bool
    lookupSpeakerTransform(const Registry& registry, ClientId speaker, glm::vec3& position, glm::vec3& velocity) const;
    void drainSpeaker(ClientId speaker, RemoteSpeaker& remote, const Registry& registry, SfxSystem& sfx);

    VoiceCapture capture_;
    std::unordered_map<int, RemoteSpeaker> speakers_;
    std::vector<SpeakingState> speaking_;
    std::vector<net::voice::ServerVoiceFrame> pendingFrames_;
    std::mutex pendingMutex_;
    bool captureInitialized_ = false;
};
