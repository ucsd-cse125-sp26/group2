/// @file VoiceCodec.hpp
/// @brief Thin RAII wrappers around the Opus encoder and decoder used by voice chat.

#pragma once

#include "network/VoiceProtocol.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

class VoiceEncoder
{
public:
    VoiceEncoder() = default;
    ~VoiceEncoder();
    VoiceEncoder(const VoiceEncoder&) = delete;
    VoiceEncoder& operator=(const VoiceEncoder&) = delete;
    VoiceEncoder(VoiceEncoder&& other) noexcept;
    VoiceEncoder& operator=(VoiceEncoder&& other) noexcept;

    [[nodiscard]] bool init();
    [[nodiscard]] bool ready() const noexcept { return encoder_ != nullptr; }
    [[nodiscard]] std::vector<std::uint8_t> encode(std::span<const float> monoPcm);
    void reset();

private:
    OpusEncoder* encoder_ = nullptr;
};

class VoiceDecoder
{
public:
    VoiceDecoder() = default;
    ~VoiceDecoder();
    VoiceDecoder(const VoiceDecoder&) = delete;
    VoiceDecoder& operator=(const VoiceDecoder&) = delete;
    VoiceDecoder(VoiceDecoder&& other) noexcept;
    VoiceDecoder& operator=(VoiceDecoder&& other) noexcept;

    [[nodiscard]] bool init();
    [[nodiscard]] bool ready() const noexcept { return decoder_ != nullptr; }
    [[nodiscard]] std::vector<float> decode(std::span<const std::uint8_t> opus, std::uint8_t frameMs);
    [[nodiscard]] std::vector<float> conceal(std::uint8_t frameMs);
    void reset();

private:
    OpusDecoder* decoder_ = nullptr;
};
