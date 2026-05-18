/// @file HmacSha256.cpp
/// @brief Minimal SHA-256 / HMAC-SHA256 implementation.

#include "HmacSha256.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace net::crypto
{
namespace
{
constexpr std::array<std::uint32_t, 64> k_round = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

std::uint32_t rotr(std::uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

std::uint32_t readBe32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

void writeBe32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
    p[3] = static_cast<std::uint8_t>(v & 0xffu);
}

void transform(std::array<std::uint32_t, 8>& state, const std::uint8_t* block)
{
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i)
        w[static_cast<std::size_t>(i)] = readBe32(block + i * 4);
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[static_cast<std::size_t>(i - 15)], 7) ^
                                 rotr(w[static_cast<std::size_t>(i - 15)], 18) ^
                                 (w[static_cast<std::size_t>(i - 15)] >> 3);
        const std::uint32_t s1 = rotr(w[static_cast<std::size_t>(i - 2)], 17) ^
                                 rotr(w[static_cast<std::size_t>(i - 2)], 19) ^
                                 (w[static_cast<std::size_t>(i - 2)] >> 10);
        w[static_cast<std::size_t>(i)] =
            w[static_cast<std::size_t>(i - 16)] + s0 + w[static_cast<std::size_t>(i - 7)] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + k_round[static_cast<std::size_t>(i)] + w[static_cast<std::size_t>(i)];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}
} // namespace

Sha256Digest sha256(const void* data, std::size_t len)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const std::size_t originalLen = len;
    std::array<std::uint32_t, 8> state = {
        0x6a09e667u,
        0xbb67ae85u,
        0x3c6ef372u,
        0xa54ff53au,
        0x510e527fu,
        0x9b05688cu,
        0x1f83d9abu,
        0x5be0cd19u,
    };

    while (len >= 64) {
        transform(state, bytes);
        bytes += 64;
        len -= 64;
    }

    std::array<std::uint8_t, 128> tail{};
    if (len > 0)
        std::memcpy(tail.data(), bytes, len);
    tail[len] = 0x80u;

    const std::uint64_t bitLen = static_cast<std::uint64_t>(originalLen) * 8ULL;

    const std::size_t finalBlockLen = len + 1 + 8 <= 64 ? 64 : 128;
    for (int i = 0; i < 8; ++i)
        tail[finalBlockLen - 1 - static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xffu);
    transform(state, tail.data());
    if (finalBlockLen == 128)
        transform(state, tail.data() + 64);

    Sha256Digest digest{};
    for (std::size_t i = 0; i < state.size(); ++i)
        writeBe32(digest.data() + i * 4, state[i]);
    return digest;
}

Sha256Digest hmacSha256(const void* key, std::size_t keyLen, const void* data, std::size_t len)
{
    std::array<std::uint8_t, 64> keyBlock{};
    if (keyLen > keyBlock.size()) {
        const Sha256Digest keyDigest = sha256(key, keyLen);
        std::memcpy(keyBlock.data(), keyDigest.data(), keyDigest.size());
    } else if (keyLen > 0) {
        std::memcpy(keyBlock.data(), key, keyLen);
    }

    std::array<std::uint8_t, 64> ipad{};
    std::array<std::uint8_t, 64> opad{};
    for (std::size_t i = 0; i < keyBlock.size(); ++i) {
        ipad[i] = static_cast<std::uint8_t>(keyBlock[i] ^ 0x36u);
        opad[i] = static_cast<std::uint8_t>(keyBlock[i] ^ 0x5cu);
    }

    std::vector<std::uint8_t> inner;
    inner.reserve(ipad.size() + len);
    inner.insert(inner.end(), ipad.begin(), ipad.end());
    if (len > 0 && data)
        inner.insert(inner.end(), static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + len);
    const Sha256Digest innerDigest = sha256(inner.data(), inner.size());

    std::array<std::uint8_t, 96> outer{};
    std::memcpy(outer.data(), opad.data(), opad.size());
    std::memcpy(outer.data() + opad.size(), innerDigest.data(), innerDigest.size());
    return sha256(outer.data(), outer.size());
}

bool constantTimeEqual(const std::uint8_t* a, const std::uint8_t* b, std::size_t len)
{
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < len; ++i)
        diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

} // namespace net::crypto
