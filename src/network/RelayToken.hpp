/// @file RelayToken.hpp
/// @brief Opaque relay authorization token shared by discovery and transport.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace net
{

inline constexpr std::size_t k_relayTokenMacBytes = 32;

struct RelayToken
{
    std::uint64_t expiresAtMs = 0;
    std::array<std::uint8_t, k_relayTokenMacBytes> mac{};
};

inline bool hasRelayToken(const RelayToken& token)
{
    if (token.expiresAtMs == 0)
        return false;
    for (std::uint8_t byte : token.mac) {
        if (byte != 0)
            return true;
    }
    return false;
}

} // namespace net
