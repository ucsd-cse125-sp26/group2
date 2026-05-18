/// @file VoiceProtocol.hpp
/// @brief Bounded Opus voice-frame packet helpers.

#pragma once

#include "ecs/components/ClientId.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace net::voice
{

inline constexpr int k_sampleRate = 48000;
inline constexpr int k_channels = 1;
inline constexpr std::uint8_t k_frameMs = 20;
inline constexpr std::uint16_t k_maxOpusBytes = 512;

struct ClientVoiceFrame
{
    std::uint16_t sequence = 0;
    std::uint8_t frameMs = k_frameMs;
    std::vector<std::uint8_t> opus;
};

struct ClientVoiceFrameView
{
    std::uint16_t sequence = 0;
    std::uint8_t frameMs = k_frameMs;
    std::span<const std::uint8_t> opus;
};

struct ServerVoiceFrame
{
    ClientId speaker{};
    std::uint16_t sequence = 0;
    std::uint8_t frameMs = k_frameMs;
    std::vector<std::uint8_t> opus;
};

[[nodiscard]] std::vector<std::uint8_t>
encodeClientFrame(std::uint16_t sequence, std::uint8_t frameMs, std::span<const std::uint8_t> opus);
[[nodiscard]] std::vector<std::uint8_t>
encodeServerFrame(ClientId speaker, std::uint16_t sequence, std::uint8_t frameMs, std::span<const std::uint8_t> opus);
[[nodiscard]] std::optional<ClientVoiceFrameView> decodeClientFrameView(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ClientVoiceFrame> decodeClientFrame(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ServerVoiceFrame> decodeServerFrame(std::span<const std::uint8_t> payload);

} // namespace net::voice
