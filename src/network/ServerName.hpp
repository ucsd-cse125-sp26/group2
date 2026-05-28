/// @file ServerName.hpp
/// @brief Shared server-name validation helpers.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace server_name
{
constexpr std::size_t k_maxBytes = 32;
constexpr std::string_view k_default = "Group 2 Server";

inline std::string clampUtf8Bytes(std::string_view value)
{
    std::size_t i = 0;
    std::size_t lastValid = 0;
    while (i < value.size() && i < k_maxBytes) {
        const auto c = static_cast<unsigned char>(value[i]);
        std::size_t len = 1;
        if (c < 0x80) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        } else {
            break;
        }

        if (i + len > value.size() || i + len > k_maxBytes)
            break;

        bool validContinuation = true;
        for (std::size_t j = 1; j < len; ++j) {
            const auto next = static_cast<unsigned char>(value[i + j]);
            if ((next & 0xC0) != 0x80) {
                validContinuation = false;
                break;
            }
        }
        if (!validContinuation)
            break;

        i += len;
        lastValid = i;
    }

    return std::string(value.substr(0, lastValid));
}

inline std::string sanitize(std::string_view value, std::string_view fallback = k_default)
{
    std::string result = clampUtf8Bytes(value);
    if (!result.empty())
        return result;

    result = clampUtf8Bytes(fallback);
    if (!result.empty())
        return result;

    return std::string(k_default);
}
} // namespace server_name
