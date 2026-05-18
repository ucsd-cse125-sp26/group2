/// @file UdpSessionTransport.hpp
/// @brief UDP-first session transport with handshake, reliability, and relay routing.

#pragma once

#include "FragmentReassembler.hpp"
#include "UdpEndpoint.hpp"

#include <SDL3/SDL_stdinc.h>

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace net
{

class UdpSessionTransport
{
public:
    enum class Mode
    {
        Client,
        Server,
    };

    enum class EventType
    {
        Connected,
        Payload,
        Disconnected,
        DirectoryControl,
    };

    struct Event
    {
        EventType type = EventType::Payload;
        std::uint64_t connectionId = 0;
        ChannelId channel = ChannelId::InputUnreliable;
        std::vector<std::uint8_t> payload;
        bool viaRelay = false;
    };

    struct Stats
    {
        std::uint64_t bytesSent = 0;
        std::uint64_t bytesRecv = 0;
        std::uint32_t packetsSent = 0;
        std::uint32_t packetsRecv = 0;
        std::uint32_t reliableRetransmits = 0;
        std::uint32_t reliablePending = 0;
        std::uint32_t activeRouteId = 0;
        float rttMs = 0.0f;
        bool relayActive = false;
        bool directActive = false;
    };

    struct RelayConfig
    {
        std::string host;
        Uint16 port = 0;
        std::uint32_t serverId = 0;
        std::uint32_t clientNonce = 0;
        bool enabled = false;
    };

    UdpSessionTransport();
    UdpSessionTransport(const UdpSessionTransport&) = delete;
    UdpSessionTransport& operator=(const UdpSessionTransport&) = delete;
    ~UdpSessionTransport() { close(); }

    bool openServer(const char* bindAddr, Uint16 port);
    bool connectClient(const char* host, Uint16 port, int timeoutMs);
    void close();

    void setRelayConfig(const RelayConfig& cfg);
    void preferRelay(bool enabled) { preferRelay_ = enabled; }

    bool pollEvent(Event& out);
    void pump();

    bool send(std::uint64_t connectionId, ChannelId channel, const void* payload, int payloadLen, int redundancy = 1);
    bool disconnect(std::uint64_t connectionId);

    [[nodiscard]] bool isOpen() const noexcept { return endpoint_.isOpen(); }
    [[nodiscard]] bool isClientConnected() const noexcept { return clientConnectionId_ != 0; }
    [[nodiscard]] std::uint64_t clientConnectionId() const noexcept { return clientConnectionId_; }
    [[nodiscard]] std::uint32_t clientNonce() const noexcept { return clientNonce_; }
    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    /// @brief Send a directory-control payload from the same UDP socket.
    bool sendDirectoryControl(const UdpEndpointAddr& dest, const void* payload, int payloadLen);

private:
    struct PendingReliable
    {
        std::uint32_t sequence = 0;
        ChannelId channel = ChannelId::ControlReliableOrdered;
        std::vector<std::uint8_t> payload;
        Uint64 lastSendMs = 0;
        std::uint8_t attempts = 0;
    };

    struct ChannelState
    {
        std::uint32_t nextSequence = 1;
        bool recvAny = false;
        std::uint32_t recvHighest = 0;
        std::uint32_t recvAckBits = 0;
        bool orderedAny = false;
        std::uint32_t orderedNext = 0;
        std::map<std::uint32_t, std::vector<std::uint8_t>> orderedBuffer;
        std::deque<PendingReliable> pending;
        FragmentReassembler reassembler;
    };

    struct Peer
    {
        std::uint64_t connectionId = 0;
        UdpEndpointAddr directAddr;
        UdpEndpointAddr relayAddr;
        std::uint32_t relayServerId = 0;
        std::uint32_t relayClientNonce = 0;
        bool hasDirect = false;
        bool hasRelay = false;
        bool useRelay = false;
        bool connectedEventSent = false;
        Uint64 lastHeardMs = 0;
        Uint64 lastKeepAliveMs = 0;
        float rttMs = 0.0f;
        std::array<ChannelState, static_cast<std::size_t>(ChannelId::Count)> channels;
    };

    static constexpr Uint64 k_keepAliveMs = 1000;
    static constexpr Uint64 k_timeoutMs = 5000;
    static constexpr Uint64 k_retransmitFloorMs = 80;
    static constexpr std::size_t k_maxReliablePending = 256;

    [[nodiscard]] static std::size_t channelIndex(ChannelId channel) noexcept;
    [[nodiscard]] static bool isReliable(ChannelId channel) noexcept;
    [[nodiscard]] static bool seqMoreRecent(std::uint32_t s1, std::uint32_t s2) noexcept;
    [[nodiscard]] static bool seqAcked(std::uint32_t seq, std::uint32_t ack, std::uint32_t ackBits) noexcept;

    bool resolveAddress(const char* host, Uint16 port, UdpEndpointAddr& out, int timeoutMs);
    void queueEvent(Event&& event);
    Peer* findPeer(std::uint64_t connectionId);
    Peer& createServerPeer(const UdpEndpointAddr& from, std::uint32_t clientNonce, bool viaRelay);

    void sendConnectionRequest(bool viaRelay);
    void sendConnectionAccepted(Peer& peer);
    bool sendPacket(Peer& peer, PacketHeader hdr, const void* payload, int payloadLen, int redundancy);
    bool sendViaRelay(Peer& peer, PacketHeader hdr, const void* payload, int payloadLen);
    bool sendDirect(const UdpEndpointAddr& dest, PacketHeader hdr, const void* payload, int payloadLen, int redundancy);

    void processDatagram(UdpReceivedMessage& msg, bool viaRelay);
    void processRelayPayload(UdpReceivedMessage& msg);
    void processPayload(Peer& peer, PacketHeader hdr, std::vector<std::uint8_t>&& payload, bool viaRelay);
    void processAcks(Peer& peer, const PacketHeader& hdr);
    void rememberReceived(ChannelState& state, std::uint32_t sequence);
    bool acceptUnreliableSequenced(ChannelState& state, std::uint32_t sequence);
    void deliverReliable(
        Peer& peer, ChannelId channel, std::uint32_t sequence, std::vector<std::uint8_t>&& payload, bool viaRelay);
    void retransmitReliable(Uint64 nowMs);
    void sendKeepAlives(Uint64 nowMs);
    void dropTimedOutPeers(Uint64 nowMs);

    Mode mode_ = Mode::Client;
    UdpEndpoint endpoint_;
    UdpEndpointAddr serverAddr_;
    UdpEndpointAddr relayAddr_;
    RelayConfig relayConfig_;
    bool preferRelay_ = false;
    std::uint32_t clientNonce_ = 0;
    std::uint64_t clientConnectionId_ = 0;
    Uint64 lastConnectAttemptMs_ = 0;
    std::mt19937_64 rng_;
    std::unordered_map<std::uint64_t, Peer> peers_;
    std::unordered_map<std::uint32_t, std::uint64_t> connectionByClientNonce_;
    std::deque<Event> events_;
    Stats stats_;
};

} // namespace net
