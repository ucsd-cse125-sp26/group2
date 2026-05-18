/// @file VoiceCodec.cpp
/// @brief Opus encoder/decoder wrappers for 48 kHz mono voice frames.

#include "VoiceCodec.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <opus.h>

namespace
{
int samplesForFrameMs(std::uint8_t frameMs)
{
    return (net::voice::k_sampleRate * static_cast<int>(frameMs)) / 1000;
}
} // namespace

VoiceEncoder::~VoiceEncoder()
{
    reset();
}

VoiceEncoder::VoiceEncoder(VoiceEncoder&& other) noexcept : encoder_(other.encoder_)
{
    other.encoder_ = nullptr;
}

VoiceEncoder& VoiceEncoder::operator=(VoiceEncoder&& other) noexcept
{
    if (this != &other) {
        reset();
        encoder_ = other.encoder_;
        other.encoder_ = nullptr;
    }
    return *this;
}

bool VoiceEncoder::init()
{
    reset();
    int err = OPUS_OK;
    encoder_ = opus_encoder_create(net::voice::k_sampleRate, net::voice::k_channels, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !encoder_) {
        SDL_Log("[voice] opus_encoder_create failed: %s", opus_strerror(err));
        encoder_ = nullptr;
        return false;
    }
    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(encoder_, OPUS_SET_DTX(1));
    return true;
}

std::vector<std::uint8_t> VoiceEncoder::encode(std::span<const float> monoPcm)
{
    if (!encoder_)
        return {};
    const int expectedSamples = samplesForFrameMs(net::voice::k_frameMs);
    if (monoPcm.size() != static_cast<std::size_t>(expectedSamples))
        return {};

    std::vector<std::uint8_t> out(net::voice::k_maxOpusBytes);
    const int bytes =
        opus_encode_float(encoder_, monoPcm.data(), expectedSamples, out.data(), static_cast<opus_int32>(out.size()));
    if (bytes <= 0) {
        SDL_Log("[voice] opus_encode_float failed: %s", opus_strerror(bytes));
        return {};
    }
    out.resize(static_cast<std::size_t>(bytes));
    return out;
}

void VoiceEncoder::reset()
{
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
}

VoiceDecoder::~VoiceDecoder()
{
    reset();
}

VoiceDecoder::VoiceDecoder(VoiceDecoder&& other) noexcept : decoder_(other.decoder_)
{
    other.decoder_ = nullptr;
}

VoiceDecoder& VoiceDecoder::operator=(VoiceDecoder&& other) noexcept
{
    if (this != &other) {
        reset();
        decoder_ = other.decoder_;
        other.decoder_ = nullptr;
    }
    return *this;
}

bool VoiceDecoder::init()
{
    reset();
    int err = OPUS_OK;
    decoder_ = opus_decoder_create(net::voice::k_sampleRate, net::voice::k_channels, &err);
    if (err != OPUS_OK || !decoder_) {
        SDL_Log("[voice] opus_decoder_create failed: %s", opus_strerror(err));
        decoder_ = nullptr;
        return false;
    }
    return true;
}

std::vector<float> VoiceDecoder::decode(std::span<const std::uint8_t> opus, std::uint8_t frameMs)
{
    if (!decoder_ || opus.empty() || opus.size() > net::voice::k_maxOpusBytes)
        return {};
    const int samples = samplesForFrameMs(frameMs);
    if (samples <= 0)
        return {};
    std::vector<float> pcm(static_cast<std::size_t>(samples));
    const int decoded =
        opus_decode_float(decoder_, opus.data(), static_cast<opus_int32>(opus.size()), pcm.data(), samples, 0);
    if (decoded <= 0) {
        SDL_Log("[voice] opus_decode_float failed: %s", opus_strerror(decoded));
        return {};
    }
    pcm.resize(static_cast<std::size_t>(decoded));
    return pcm;
}

std::vector<float> VoiceDecoder::conceal(std::uint8_t frameMs)
{
    if (!decoder_)
        return {};
    const int samples = samplesForFrameMs(frameMs);
    if (samples <= 0)
        return {};
    std::vector<float> pcm(static_cast<std::size_t>(samples));
    const int decoded = opus_decode_float(decoder_, nullptr, 0, pcm.data(), samples, 0);
    if (decoded <= 0)
        std::fill(pcm.begin(), pcm.end(), 0.0f);
    else
        pcm.resize(static_cast<std::size_t>(decoded));
    return pcm;
}

void VoiceDecoder::reset()
{
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
}
