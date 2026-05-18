/// @file HmacSha256.hpp
/// @brief Small SHA-256 and HMAC-SHA256 helpers for relay-token signing.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace net::crypto
{

using Sha256Digest = std::array<std::uint8_t, 32>;

Sha256Digest sha256(const void* data, std::size_t len);
Sha256Digest hmacSha256(const void* key, std::size_t keyLen, const void* data, std::size_t len);

bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t len);

} // namespace net::crypto
