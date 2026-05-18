/// @file PacketHeader.hpp
/// @brief Wire format helpers for the UDP-first transport layer.
///
/// Protocol v2 puts session identity, route selection, channel sequence,
/// selective ack state, and fragmentation metadata in every UDP datagram.
/// The wire format is explicitly little-endian so future Steam / relay
/// backends can share the same payload contract without relying on host
/// endianness.

#pragma once

#include <SDL3/SDL_stdinc.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace net
{

/// @brief Magic bytes identifying our protocol. ASCII "G2" little-endian.
inline constexpr std::uint16_t k_protocolMagic = 0x3247;

/// @brief Wire-format version. Bump on any incompatible layout change.
inline constexpr std::uint8_t k_protocolVersion = 2;

/// @brief Maximum total UDP datagram bytes we intentionally emit.
///
/// 1200 bytes is the safe MTU floor for public Internet paths. Larger
/// logical messages are fragmented above UDP by the transport layer.
inline constexpr int k_maxPacketBytes = 1200;

/// @brief Packet kind discriminator (PacketHeader::kind field).
enum class PacketKind : std::uint8_t
{
    Payload = 0,
    ConnectionRequest = 1,
    ConnectionAccepted = 2,
    Disconnect = 3,
    KeepAlive = 4,
    RelayPayload = 5,
    DirectoryControl = 6,
};

/// @brief Per-channel reliability + ordering semantics.
enum class ChannelId : std::uint8_t
{
    InputUnreliable = 0,
    SnapshotUnreliableSequenced = 1,
    ControlReliableOrdered = 2,
    EventReliableOrdered = 3,

    Count = 4,

    // Compatibility aliases for code written during the UDP-sidecar rollout.
    Unreliable = InputUnreliable,
    UnreliableSequenced = SnapshotUnreliableSequenced,
    ReliableOrdered = EventReliableOrdered,
    ReliableUnordered = ControlReliableOrdered,
};

inline constexpr std::uint8_t k_flagFragmented = 0x01;
inline constexpr std::uint8_t k_flagEncrypted = 0x02;
inline constexpr std::uint8_t k_flagRelayPreferred = 0x04;

#pragma pack(push, 1)
/// @brief 36-byte header at the start of every UDP datagram.
struct PacketHeader
{
    std::uint16_t magic = k_protocolMagic;
    std::uint8_t version = k_protocolVersion;
    std::uint8_t kind = static_cast<std::uint8_t>(PacketKind::Payload);
    std::uint64_t connectionId = 0; ///< Server-assigned session id; 0 pre-handshake.
    std::uint32_t sequence = 0;     ///< Per-(connection, channel) sequence.
    std::uint32_t ack = 0;          ///< Most-recent sequence received on this channel.
    std::uint32_t ackBits = 0;      ///< Bit i acks ack-(i+1).
    std::uint16_t routeId = 0;      ///< 0 direct, non-zero relay/future Steam route.
    std::uint8_t channel = static_cast<std::uint8_t>(ChannelId::InputUnreliable);
    std::uint8_t flags = 0;
    std::uint16_t fragmentInfo = 0;  ///< hi 8 = fragment index, lo 8 = fragment count.
    std::uint16_t fragmentGroup = 0; ///< Logical fragmented-message id.
    std::uint32_t _pad = 0;
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == 36, "PacketHeader must be exactly 36 bytes on the wire");

/// @brief Maximum payload bytes a single non-fragmented datagram can carry.
inline constexpr int k_maxPayloadBytes = k_maxPacketBytes - static_cast<int>(sizeof(PacketHeader));

inline void writeU16Le(std::uint8_t* out, std::uint16_t v)
{
    out[0] = static_cast<std::uint8_t>(v & 0xffu);
    out[1] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
}

inline void writeU32Le(std::uint8_t* out, std::uint32_t v)
{
    out[0] = static_cast<std::uint8_t>(v & 0xffu);
    out[1] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
    out[2] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
    out[3] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
}

inline void writeU64Le(std::uint8_t* out, std::uint64_t v)
{
    writeU32Le(out, static_cast<std::uint32_t>(v & 0xffffffffULL));
    writeU32Le(out + 4, static_cast<std::uint32_t>((v >> 32) & 0xffffffffULL));
}

inline std::uint16_t readU16Le(const std::uint8_t* in)
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[0]) |
                                      static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8));
}

inline std::uint32_t readU32Le(const std::uint8_t* in)
{
    return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8) |
           (static_cast<std::uint32_t>(in[2]) << 16) | (static_cast<std::uint32_t>(in[3]) << 24);
}

inline std::uint64_t readU64Le(const std::uint8_t* in)
{
    return static_cast<std::uint64_t>(readU32Le(in)) | (static_cast<std::uint64_t>(readU32Le(in + 4)) << 32);
}

inline void encodePacketHeader(const PacketHeader& hdr, std::uint8_t* out)
{
    writeU16Le(out + 0, hdr.magic);
    out[2] = hdr.version;
    out[3] = hdr.kind;
    writeU64Le(out + 4, hdr.connectionId);
    writeU32Le(out + 12, hdr.sequence);
    writeU32Le(out + 16, hdr.ack);
    writeU32Le(out + 20, hdr.ackBits);
    writeU16Le(out + 24, hdr.routeId);
    out[26] = hdr.channel;
    out[27] = hdr.flags;
    writeU16Le(out + 28, hdr.fragmentInfo);
    writeU16Le(out + 30, hdr.fragmentGroup);
    writeU32Le(out + 32, hdr._pad);
}

inline bool decodePacketHeader(const std::uint8_t* data, std::size_t len, PacketHeader& out)
{
    if (len < sizeof(PacketHeader))
        return false;

    out.magic = readU16Le(data + 0);
    out.version = data[2];
    out.kind = data[3];
    out.connectionId = readU64Le(data + 4);
    out.sequence = readU32Le(data + 12);
    out.ack = readU32Le(data + 16);
    out.ackBits = readU32Le(data + 20);
    out.routeId = readU16Le(data + 24);
    out.channel = data[26];
    out.flags = data[27];
    out.fragmentInfo = readU16Le(data + 28);
    out.fragmentGroup = readU16Le(data + 30);
    out._pad = readU32Le(data + 32);
    return out.magic == k_protocolMagic && out.version == k_protocolVersion;
}

inline std::vector<std::uint8_t> makeDatagram(PacketHeader hdr, const void* payload, int payloadLen)
{
    if (payloadLen < 0)
        payloadLen = 0;

    hdr.magic = k_protocolMagic;
    hdr.version = k_protocolVersion;
    hdr._pad = 0;

    std::vector<std::uint8_t> bytes(sizeof(PacketHeader) + static_cast<std::size_t>(payloadLen));
    encodePacketHeader(hdr, bytes.data());
    if (payloadLen > 0 && payload)
        std::memcpy(bytes.data() + sizeof(PacketHeader), payload, static_cast<std::size_t>(payloadLen));
    return bytes;
}

} // namespace net
