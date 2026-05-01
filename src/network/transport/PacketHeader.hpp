/// @file PacketHeader.hpp
/// @brief Wire format for the UDP transport layer.
///
/// Phase 3d: every UDP datagram begins with this 16-byte header so the
/// receiver can demux by channel, drop stale snapshots by sequence
/// number, reassemble fragments, and route to the right connection.
///
/// **Layout** — 16 bytes, little-endian on the wire (matches all our
/// target platforms; saves a byte-swap on send/recv).
///
/// ```
///  0  1   2  3   4    5    6    7
/// +----+--+--+----------+--+--+----+
/// |magc| v|kn| connId    |seq    |
/// +----+--+--+-+--------+--+--+----+
///  8  9   10 11  12   13   14  15
/// +-+----+--+--+----+--+--+--+--+
/// |chan|flg| frag(idx, count) |
/// +----+--+--+--+----+--+--+--+
/// ```
///
/// * **magic (u16)**: `k_protocolMagic` = 0x4732 ("G2"). Defends against
///   stray UDP traffic from unrelated services landing on our port.
/// * **version (u8)**: bumped on incompatible wire-format changes.
/// * **kind (u8)**: PacketKind discriminator (handshake / data / etc).
/// * **connectionId (u32)**: server-assigned, identifies the peer. 0 in
///   pre-handshake datagrams.
/// * **sequence (u16)**: per-(connection, channel) wrap-around counter.
///   Receiver uses Glenn-Fiedler `seq_more_recent` to drop stale.
/// * **channel (u8)**: ChannelId — picks reliability + ordering rules.
/// * **flags (u8)**: bit 0 = fragmented; bit 1 = encrypted (future).
/// * **fragmentInfo (u16)**: hi 8 = fragment index, lo 8 = fragment
///   count. 0 if not fragmented.
///
/// Stage 3d-1 ships only the struct + magic numbers; later stages
/// populate sequence/channel/fragmentation as those features land.

#pragma once

#include <SDL3/SDL_stdinc.h>

#include <cstdint>

namespace net
{

/// @brief Magic bytes identifying our protocol. ASCII "G2" little-endian.
inline constexpr uint16_t k_protocolMagic = 0x3247;

/// @brief Wire-format version. Bump on any layout change.
inline constexpr uint8_t k_protocolVersion = 1;

/// @brief Maximum total UDP payload (header + data) in bytes.
///
/// 1200 is the safe MTU floor for the public Internet. Routers can
/// fragment or drop bigger datagrams. Stage 3d-4 adds application-layer
/// fragmentation for messages that exceed this.
inline constexpr int k_maxPacketBytes = 1200;

/// @brief Packet kind discriminator (PacketHeader::kind field).
enum class PacketKind : uint8_t
{
    /// @brief Application data ride this — snapshots, inputs, events.
    Payload = 0,

    /// @brief Future: connection handshake, keep-alive, disconnect.
    /// Stage 3d-1 doesn't use these yet — connectionId is established
    /// over the existing TCP path until handshake-over-UDP lands.
    ConnectionRequest = 1,
    ConnectionAccepted = 2,
    Disconnect = 3,
    KeepAlive = 4,
};

/// @brief Per-channel reliability + ordering semantics.
///
/// Stage 3d-1 ships only the enum; each channel's actual behaviour
/// arrives with the packet types that use it.
enum class ChannelId : uint8_t
{
    /// @brief No reliability. Drop stale by sequence comparison.
    /// Used for: snapshots, inputs (with redundancy at app layer).
    Unreliable = 0,

    /// @brief No retransmit, but receiver discards packets older than
    /// the newest sequence already seen. Future: VFX bursts.
    UnreliableSequenced = 1,

    /// @brief Retransmit until acked, in-order delivery. Future:
    /// match-state, kill events, score updates.
    ReliableOrdered = 2,

    /// @brief Retransmit until acked, no ordering required. Future: RPC.
    ReliableUnordered = 3,

    Count = 4
};

#pragma pack(push, 1)
/// @brief 16-byte header at the start of every UDP datagram we send.
struct PacketHeader
{
    uint16_t magic;        ///< k_protocolMagic
    uint8_t version;       ///< k_protocolVersion
    uint8_t kind;          ///< PacketKind
    uint32_t connectionId; ///< Server-assigned; 0 pre-handshake
    uint16_t sequence;     ///< Per-(connection, channel)
    uint8_t channel;       ///< ChannelId
    uint8_t flags;         ///< bit 0 = fragmented, bit 1 = encrypted (future)
    uint16_t fragmentInfo; ///< hi 8 = fragment index, lo 8 = count; 0 if not fragmented
    uint16_t _pad;         ///< Reserved; zero.  Brings the header to 16 bytes.
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be exactly 16 bytes on the wire");

/// @brief Maximum payload bytes a single (non-fragmented) datagram can carry.
inline constexpr int k_maxPayloadBytes = k_maxPacketBytes - static_cast<int>(sizeof(PacketHeader));

} // namespace net
