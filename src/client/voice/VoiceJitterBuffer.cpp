/// @file VoiceJitterBuffer.cpp
/// @brief Bounded jitter buffer implementation.

#include "VoiceJitterBuffer.hpp"

#include <algorithm>

namespace
{
bool seqLess(std::uint16_t a, std::uint16_t b) noexcept
{
    return static_cast<std::int16_t>(a - b) < 0;
}

bool seqEqualOrOlder(std::uint16_t a, std::uint16_t b) noexcept
{
    return a == b || seqLess(a, b);
}
} // namespace

VoiceJitterBuffer::VoiceJitterBuffer(std::size_t prebufferFrames, std::size_t maxFrames)
    : prebufferFrames_(std::max<std::size_t>(1, prebufferFrames))
    , maxFrames_(std::max(prebufferFrames_ + 1u, maxFrames))
{}

void VoiceJitterBuffer::push(std::uint16_t sequence, std::uint8_t frameMs, std::span<const std::uint8_t> opus)
{
    if (opus.empty())
        return;
    if (hasExpected_ && tooOld(sequence))
        return;
    const auto duplicate = std::find_if(
        frames_.begin(), frames_.end(), [&](const EncodedFrame& frame) { return frame.sequence == sequence; });
    if (duplicate != frames_.end())
        return;

    EncodedFrame frame;
    frame.sequence = sequence;
    frame.frameMs = frameMs;
    frame.opus.assign(opus.begin(), opus.end());
    frames_.push_back(std::move(frame));
    std::sort(frames_.begin(), frames_.end(), [](const EncodedFrame& a, const EncodedFrame& b) {
        return seqLess(a.sequence, b.sequence);
    });
    while (frames_.size() > maxFrames_)
        frames_.erase(frames_.begin());
}

std::optional<VoiceJitterBuffer::EncodedFrame> VoiceJitterBuffer::pop()
{
    if (frames_.empty())
        return std::nullopt;
    if (!hasExpected_) {
        if (frames_.size() < prebufferFrames_)
            return std::nullopt;
        expectedSequence_ = firstSequence();
        hasExpected_ = true;
    }

    const auto it = std::find_if(
        frames_.begin(), frames_.end(), [&](const EncodedFrame& frame) { return frame.sequence == expectedSequence_; });
    if (it != frames_.end()) {
        EncodedFrame frame = std::move(*it);
        frames_.erase(it);
        ++expectedSequence_;
        return frame;
    }

    if (frames_.size() >= prebufferFrames_ + 2u) {
        EncodedFrame lost;
        lost.sequence = expectedSequence_++;
        lost.frameMs = 20;
        lost.lost = true;
        return lost;
    }
    return std::nullopt;
}

void VoiceJitterBuffer::reset()
{
    frames_.clear();
    expectedSequence_ = 0;
    hasExpected_ = false;
}

bool VoiceJitterBuffer::tooOld(std::uint16_t sequence) const noexcept
{
    return seqEqualOrOlder(sequence, static_cast<std::uint16_t>(expectedSequence_ - 1u));
}

std::uint16_t VoiceJitterBuffer::firstSequence() const noexcept
{
    return frames_.empty() ? 0 : frames_.front().sequence;
}
