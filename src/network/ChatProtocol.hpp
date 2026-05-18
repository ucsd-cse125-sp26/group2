/// @file ChatProtocol.hpp
/// @brief Bounded text-chat packet helpers shared by client, server, and tests.

#pragma once

#include "ecs/components/ClientId.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace net::chat
{

inline constexpr std::uint16_t k_maxChatBytes = 240;

struct ClientTextChat
{
    std::uint16_t clientSeq = 0;
    std::string message;
};

struct ServerTextChat
{
    ClientId sender{};
    std::uint32_t serverSeq = 0;
    std::string message;
};

[[nodiscard]] bool isValidUtf8(std::string_view text) noexcept;
[[nodiscard]] std::string sanitizeUtf8(std::string_view text);
[[nodiscard]] std::vector<std::uint8_t> encodeClientText(std::uint16_t clientSeq, std::string_view text);
[[nodiscard]] std::vector<std::uint8_t>
encodeServerText(ClientId sender, std::uint32_t serverSeq, std::string_view text);
[[nodiscard]] std::optional<ClientTextChat> decodeClientText(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<ServerTextChat> decodeServerText(std::span<const std::uint8_t> payload);

} // namespace net::chat
