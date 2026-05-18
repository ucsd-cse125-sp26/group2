/// @file ChatProtocol.cpp
/// @brief Text-chat packet helpers.

#include "ChatProtocol.hpp"

#include "network/PacketType.hpp"

#include <algorithm>
#include <utility>

namespace net::chat
{
namespace
{
void writeU16Le(std::uint8_t* out, std::uint16_t value)
{
    out[0] = static_cast<std::uint8_t>(value & 0xffu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
}

void writeU32Le(std::uint8_t* out, std::uint32_t value)
{
    out[0] = static_cast<std::uint8_t>(value & 0xffu);
    out[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    out[2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    out[3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::uint16_t readU16Le(const std::uint8_t* in)
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[0]) |
                                      static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8));
}

std::uint32_t readU32Le(const std::uint8_t* in)
{
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
}

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    const std::size_t off = out.size();
    out.resize(off + sizeof(std::uint16_t));
    writeU16Le(out.data() + off, value);
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    const std::size_t off = out.size();
    out.resize(off + sizeof(std::uint32_t));
    writeU32Le(out.data() + off, value);
}

void appendI32(std::vector<std::uint8_t>& out, std::int32_t value)
{
    appendU32(out, static_cast<std::uint32_t>(value));
}

[[nodiscard]] bool isControlByte(unsigned char c) noexcept
{
    return c < 0x20 || c == 0x7f;
}
} // namespace

bool isValidUtf8(std::string_view text) noexcept
{
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c <= 0x7f) {
            ++i;
            continue;
        }

        int needed = 0;
        std::uint32_t codepoint = 0;
        if ((c & 0xe0u) == 0xc0u) {
            needed = 1;
            codepoint = c & 0x1fu;
            if (codepoint == 0)
                return false; // overlong ASCII
        } else if ((c & 0xf0u) == 0xe0u) {
            needed = 2;
            codepoint = c & 0x0fu;
        } else if ((c & 0xf8u) == 0xf0u) {
            needed = 3;
            codepoint = c & 0x07u;
        } else {
            return false;
        }

        if (i + static_cast<std::size_t>(needed) >= text.size())
            return false;
        for (int j = 1; j <= needed; ++j) {
            const auto cc = static_cast<unsigned char>(text[i + static_cast<std::size_t>(j)]);
            if ((cc & 0xc0u) != 0x80u)
                return false;
            codepoint = (codepoint << 6u) | (cc & 0x3fu);
        }

        if ((needed == 2 && codepoint < 0x800u) || (needed == 3 && codepoint < 0x10000u))
            return false;
        if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu))
            return false;
        i += static_cast<std::size_t>(needed + 1);
    }
    return true;
}

std::string sanitizeUtf8(std::string_view text)
{
    if (!isValidUtf8(text))
        return {};

    std::string out;
    out.reserve(std::min<std::size_t>(text.size(), k_maxChatBytes));
    bool previousSpace = true;
    for (char raw : text) {
        const auto c = static_cast<unsigned char>(raw);
        if (isControlByte(c))
            continue;
        const bool asciiSpace = c <= 0x7f && c == ' ';
        if (asciiSpace && previousSpace)
            continue;
        if (out.size() >= k_maxChatBytes)
            break;
        out.push_back(static_cast<char>(c));
        previousSpace = asciiSpace;
    }

    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    if (!isValidUtf8(out))
        return {};
    return out;
}

std::vector<std::uint8_t> encodeClientText(std::uint16_t clientSeq, std::string_view text)
{
    const std::string clean = sanitizeUtf8(text);
    if (clean.empty())
        return {};

    std::vector<std::uint8_t> out;
    out.reserve(1 + sizeof(std::uint16_t) * 2 + clean.size());
    out.push_back(static_cast<std::uint8_t>(PacketType::TEXT_CHAT));
    appendU16(out, clientSeq);
    appendU16(out, static_cast<std::uint16_t>(clean.size()));
    out.insert(out.end(), clean.begin(), clean.end());
    return out;
}

std::vector<std::uint8_t> encodeServerText(ClientId sender, std::uint32_t serverSeq, std::string_view text)
{
    const std::string clean = sanitizeUtf8(text);
    if (clean.empty())
        return {};

    std::vector<std::uint8_t> out;
    out.reserve(1 + sizeof(std::int32_t) + sizeof(std::uint32_t) + sizeof(std::uint16_t) + clean.size());
    out.push_back(static_cast<std::uint8_t>(PacketType::TEXT_CHAT));
    appendI32(out, static_cast<std::int32_t>(sender.value));
    appendU32(out, serverSeq);
    appendU16(out, static_cast<std::uint16_t>(clean.size()));
    out.insert(out.end(), clean.begin(), clean.end());
    return out;
}

std::optional<ClientTextChat> decodeClientText(std::span<const std::uint8_t> payload)
{
    constexpr std::size_t k_header = 1 + sizeof(std::uint16_t) + sizeof(std::uint16_t);
    if (payload.size() < k_header || payload[0] != static_cast<std::uint8_t>(PacketType::TEXT_CHAT))
        return std::nullopt;
    const std::uint16_t clientSeq = readU16Le(payload.data() + 1);
    const std::uint16_t len = readU16Le(payload.data() + 1 + sizeof(std::uint16_t));
    if (len > k_maxChatBytes || payload.size() != k_header + len)
        return std::nullopt;
    const auto* begin = reinterpret_cast<const char*>(payload.data() + k_header);
    std::string clean = sanitizeUtf8(std::string_view(begin, len));
    if (clean.empty())
        return std::nullopt;
    return ClientTextChat{.clientSeq = clientSeq, .message = std::move(clean)};
}

std::optional<ServerTextChat> decodeServerText(std::span<const std::uint8_t> payload)
{
    constexpr std::size_t k_header = 1 + sizeof(std::int32_t) + sizeof(std::uint32_t) + sizeof(std::uint16_t);
    if (payload.size() < k_header || payload[0] != static_cast<std::uint8_t>(PacketType::TEXT_CHAT))
        return std::nullopt;

    const auto senderRaw = static_cast<std::int32_t>(readU32Le(payload.data() + 1));
    const std::uint32_t serverSeq = readU32Le(payload.data() + 1 + sizeof(std::int32_t));
    const std::uint16_t len = readU16Le(payload.data() + 1 + sizeof(std::int32_t) + sizeof(std::uint32_t));
    if (len > k_maxChatBytes || payload.size() != k_header + len)
        return std::nullopt;
    const auto* begin = reinterpret_cast<const char*>(payload.data() + k_header);
    std::string clean = sanitizeUtf8(std::string_view(begin, len));
    if (clean.empty())
        return std::nullopt;
    return ServerTextChat{.sender = ClientId{senderRaw}, .serverSeq = serverSeq, .message = std::move(clean)};
}

} // namespace net::chat
