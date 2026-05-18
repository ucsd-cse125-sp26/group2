/// @file VoiceProtocol.cpp
/// @brief Opus voice-frame packet helpers.

#include "VoiceProtocol.hpp"

#include "network/PacketType.hpp"

#include <cstddef>

namespace net::voice
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

[[nodiscard]] bool validFrame(std::uint8_t frameMs, std::size_t opusBytes) noexcept
{
    const bool supportedDuration = frameMs == 10 || frameMs == 20 || frameMs == 40 || frameMs == 60;
    return supportedDuration && opusBytes > 0 && opusBytes <= k_maxOpusBytes;
}
} // namespace

std::vector<std::uint8_t>
encodeClientFrame(std::uint16_t sequence, std::uint8_t frameMs, std::span<const std::uint8_t> opus)
{
    if (!validFrame(frameMs, opus.size()))
        return {};
    std::vector<std::uint8_t> out;
    out.reserve(1 + sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint16_t) + opus.size());
    out.push_back(static_cast<std::uint8_t>(PacketType::VOICE_FRAME));
    appendU16(out, sequence);
    out.push_back(frameMs);
    appendU16(out, static_cast<std::uint16_t>(opus.size()));
    out.insert(out.end(), opus.begin(), opus.end());
    return out;
}

std::vector<std::uint8_t>
encodeServerFrame(ClientId speaker, std::uint16_t sequence, std::uint8_t frameMs, std::span<const std::uint8_t> opus)
{
    if (!validFrame(frameMs, opus.size()))
        return {};
    std::vector<std::uint8_t> out;
    out.reserve(1 + sizeof(std::int32_t) + sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint16_t) +
                opus.size());
    out.push_back(static_cast<std::uint8_t>(PacketType::VOICE_FRAME));
    appendU32(out, static_cast<std::uint32_t>(speaker.value));
    appendU16(out, sequence);
    out.push_back(frameMs);
    appendU16(out, static_cast<std::uint16_t>(opus.size()));
    out.insert(out.end(), opus.begin(), opus.end());
    return out;
}

std::optional<ClientVoiceFrame> decodeClientFrame(std::span<const std::uint8_t> payload)
{
    const auto view = decodeClientFrameView(payload);
    if (!view)
        return std::nullopt;
    ClientVoiceFrame frame;
    frame.sequence = view->sequence;
    frame.frameMs = view->frameMs;
    frame.opus.assign(view->opus.begin(), view->opus.end());
    return frame;
}

std::optional<ClientVoiceFrameView> decodeClientFrameView(std::span<const std::uint8_t> payload)
{
    constexpr std::size_t k_header = 1 + sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint16_t);
    if (payload.size() < k_header || payload[0] != static_cast<std::uint8_t>(PacketType::VOICE_FRAME))
        return std::nullopt;
    const std::uint16_t sequence = readU16Le(payload.data() + 1);
    const std::uint8_t frameMs = payload[1 + sizeof(std::uint16_t)];
    const std::uint16_t opusLen = readU16Le(payload.data() + 1 + sizeof(std::uint16_t) + sizeof(std::uint8_t));
    if (!validFrame(frameMs, opusLen) || payload.size() != k_header + opusLen)
        return std::nullopt;
    ClientVoiceFrameView frame;
    frame.sequence = sequence;
    frame.frameMs = frameMs;
    frame.opus = payload.subspan(k_header, opusLen);
    return frame;
}

std::optional<ServerVoiceFrame> decodeServerFrame(std::span<const std::uint8_t> payload)
{
    constexpr std::size_t k_header =
        1 + sizeof(std::int32_t) + sizeof(std::uint16_t) + sizeof(std::uint8_t) + sizeof(std::uint16_t);
    if (payload.size() < k_header || payload[0] != static_cast<std::uint8_t>(PacketType::VOICE_FRAME))
        return std::nullopt;
    ServerVoiceFrame frame;
    frame.speaker.value = static_cast<int>(static_cast<std::int32_t>(readU32Le(payload.data() + 1)));
    frame.sequence = readU16Le(payload.data() + 1 + sizeof(std::int32_t));
    frame.frameMs = payload[1 + sizeof(std::int32_t) + sizeof(std::uint16_t)];
    const std::uint16_t opusLen =
        readU16Le(payload.data() + 1 + sizeof(std::int32_t) + sizeof(std::uint16_t) + sizeof(std::uint8_t));
    if (!validFrame(frame.frameMs, opusLen) || payload.size() != k_header + opusLen)
        return std::nullopt;
    frame.opus.assign(payload.begin() + static_cast<std::ptrdiff_t>(k_header), payload.end());
    return frame;
}

} // namespace net::voice
