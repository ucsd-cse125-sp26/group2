/// @file VoiceCapture.cpp
/// @brief SDL recording plus Opus voice encoding.

#include "VoiceCapture.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <span>

bool VoiceCapture::init()
{
    quit();
    if (!encoder_.init())
        return false;

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32LE;
    spec.channels = net::voice::k_channels;
    spec.freq = net::voice::k_sampleRate;
    captureStream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, nullptr, nullptr);
    if (!captureStream_) {
        SDL_Log("[voice] SDL_OpenAudioDeviceStream(recording) failed: %s", SDL_GetError());
        encoder_.reset();
        return false;
    }
    SDL_PauseAudioStreamDevice(captureStream_);
    return true;
}

void VoiceCapture::quit()
{
    if (captureStream_) {
        SDL_DestroyAudioStream(captureStream_);
        captureStream_ = nullptr;
    }
    encoder_.reset();
    capturePcm_.clear();
    captureReadOffset_ = 0;
    transmitting_ = false;
}

void VoiceCapture::setPushToTalk(bool active)
{
    if (!captureStream_ || active == transmitting_)
        return;
    transmitting_ = active;
    capturePcm_.clear();
    captureReadOffset_ = 0;
    SDL_ClearAudioStream(captureStream_);
    if (transmitting_)
        SDL_ResumeAudioStreamDevice(captureStream_);
    else
        SDL_PauseAudioStreamDevice(captureStream_);
}

std::vector<VoiceCapture::EncodedFrame> VoiceCapture::poll()
{
    std::vector<EncodedFrame> frames;
    if (!ready() || !transmitting_)
        return frames;

    const int availableBytes = SDL_GetAudioStreamAvailable(captureStream_);
    if (availableBytes > 0) {
        std::vector<float> incoming(static_cast<std::size_t>(availableBytes) / sizeof(float));
        const int readBytes =
            SDL_GetAudioStreamData(captureStream_, incoming.data(), static_cast<int>(incoming.size() * sizeof(float)));
        if (readBytes > 0) {
            incoming.resize(static_cast<std::size_t>(readBytes) / sizeof(float));
            capturePcm_.insert(capturePcm_.end(), incoming.begin(), incoming.end());
        }
    }

    if (captureReadOffset_ > capturePcm_.size())
        captureReadOffset_ = capturePcm_.size();

    const std::size_t availableSamples = capturePcm_.size() - captureReadOffset_;
    if (availableSamples > kMaxAccumulatedSamples) {
        captureReadOffset_ = capturePcm_.size() - kMaxAccumulatedSamples;
    }

    while (capturePcm_.size() - captureReadOffset_ >= static_cast<std::size_t>(kFrameSamples)) {
        const std::span<const float> frame(capturePcm_.data() + captureReadOffset_,
                                           static_cast<std::size_t>(kFrameSamples));
        if (framePassesNoiseGate(frame)) {
            EncodedFrame encoded;
            encoded.sequence = nextSequence_++;
            encoded.opus = encoder_.encode(frame);
            if (!encoded.opus.empty())
                frames.push_back(std::move(encoded));
        }
        captureReadOffset_ += static_cast<std::size_t>(kFrameSamples);
        if (frames.size() >= 4)
            break;
    }

    if (captureReadOffset_ == capturePcm_.size()) {
        capturePcm_.clear();
        captureReadOffset_ = 0;
    } else if (captureReadOffset_ >= static_cast<std::size_t>(kFrameSamples * 4)) {
        capturePcm_.erase(capturePcm_.begin(), capturePcm_.begin() + static_cast<std::ptrdiff_t>(captureReadOffset_));
        captureReadOffset_ = 0;
    }
    return frames;
}

bool VoiceCapture::framePassesNoiseGate(std::span<const float> frame) const noexcept
{
    float sumSquares = 0.0f;
    for (float sample : frame)
        sumSquares += sample * sample;
    const float rms = std::sqrt(sumSquares / static_cast<float>(std::max<std::size_t>(1, frame.size())));
    return rms > 0.0085f;
}
