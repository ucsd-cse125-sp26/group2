/// @file VoiceJitterBuffer.hpp
/// @brief Small bounded sequence buffer for unreliable voice frames.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

class VoiceJitterBuffer
{
public:
    struct EncodedFrame
    {
        std::uint16_t sequence = 0;
        std::uint8_t frameMs = 20;
        std::vector<std::uint8_t> opus;
        bool lost = false;
    };

    explicit VoiceJitterBuffer(std::size_t prebufferFrames = 2, std::size_t maxFrames = 10);

    void push(std::uint16_t sequence, std::uint8_t frameMs, std::span<const std::uint8_t> opus);
    [[nodiscard]] std::optional<EncodedFrame> pop();
    [[nodiscard]] std::size_t size() const noexcept { return frames_.size(); }
    void reset();

private:
    [[nodiscard]] bool tooOld(std::uint16_t sequence) const noexcept;
    [[nodiscard]] std::uint16_t firstSequence() const noexcept;

    std::size_t prebufferFrames_ = 2;
    std::size_t maxFrames_ = 10;
    std::vector<EncodedFrame> frames_;
    std::uint16_t expectedSequence_ = 0;
    bool hasExpected_ = false;
};
