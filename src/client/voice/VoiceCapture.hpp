/// @file VoiceCapture.hpp
/// @brief SDL recording plus Opus encoding for push-to-talk voice chat.

#pragma once

#include "VoiceCodec.hpp"

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <vector>

class VoiceCapture
{
public:
    struct EncodedFrame
    {
        std::uint16_t sequence = 0;
        std::uint8_t frameMs = net::voice::k_frameMs;
        std::vector<std::uint8_t> opus;
    };

    bool init();
    void quit();
    void setPushToTalk(bool active);
    [[nodiscard]] bool ready() const noexcept { return captureStream_ != nullptr && encoder_.ready(); }
    [[nodiscard]] bool transmitting() const noexcept { return transmitting_; }
    [[nodiscard]] std::vector<EncodedFrame> poll();

private:
    static constexpr int kFrameSamples = net::voice::k_sampleRate * net::voice::k_frameMs / 1000;
    static constexpr std::size_t kMaxAccumulatedSamples = static_cast<std::size_t>(kFrameSamples * 8);

    [[nodiscard]] bool framePassesNoiseGate(std::span<const float> frame) const noexcept;

    SDL_AudioStream* captureStream_ = nullptr;
    VoiceEncoder encoder_;
    std::vector<float> capturePcm_;
    std::size_t captureReadOffset_ = 0;
    std::uint16_t nextSequence_ = 0;
    bool transmitting_ = false;
};
