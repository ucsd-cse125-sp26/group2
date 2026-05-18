/// @file GlobalServerAdvertiser.cpp
/// @brief Server-side publisher for the global directory service.

#include "GlobalServerAdvertiser.hpp"

#include "network/MessageStream.hpp"
#include "network/transport/PacketHeader.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <algorithm>
#include <cstring>

namespace
{
NET_StreamSocket* connectDirectory(const GlobalDiscoveryConfig& cfg, int timeoutMs)
{
    NET_Address* addr = NET_ResolveHostname(cfg.directoryHost.c_str());
    if (!addr || NET_WaitUntilResolved(addr, timeoutMs) != NET_SUCCESS) {
        if (addr)
            NET_UnrefAddress(addr);
        return nullptr;
    }

    NET_StreamSocket* socket = NET_CreateClient(addr, cfg.directoryTcpPort);
    NET_UnrefAddress(addr);
    if (!socket)
        return nullptr;

    NET_SetStreamSocketNoDelay(socket, true);
    if (NET_WaitUntilConnected(socket, timeoutMs) != NET_SUCCESS) {
        NET_DestroyStreamSocket(socket);
        return nullptr;
    }
    return socket;
}

bool sendUdp(net::UdpEndpoint& udp,
             const std::string& host,
             Uint16 port,
             const std::vector<std::uint8_t>& payload,
             std::uint32_t connectionId)
{
    NET_Address* addr = NET_ResolveHostname(host.c_str());
    if (!addr || NET_WaitUntilResolved(addr, 1000) != NET_SUCCESS) {
        if (addr)
            NET_UnrefAddress(addr);
        return false;
    }

    net::UdpEndpointAddr dest;
    dest.addr = addr;
    dest.port = port;
    net::PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(net::PacketKind::KeepAlive);
    hdr.connectionId = connectionId;
    hdr.channel = static_cast<std::uint8_t>(net::ChannelId::Unreliable);
    const bool ok = udp.send(dest, hdr, payload.data(), static_cast<int>(payload.size()));
    dest.release();
    return ok;
}

std::vector<std::uint8_t> makePunchProbe(std::uint32_t nonce)
{
    std::vector<std::uint8_t> payload;
    payload.push_back(static_cast<std::uint8_t>(net::discovery::DirectoryUdpMessage::PunchProbe));
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&nonce);
    payload.insert(payload.end(), bytes, bytes + sizeof(nonce));
    return payload;
}
} // namespace

bool GlobalServerAdvertiser::start(const GlobalDiscoveryConfig& cfg,
                                   Uint16 gamePort,
                                   std::function<int()> currentPlayersFn)
{
    if (!cfg.enabled || !cfg.advertiseServer)
        return false;
    if (thread_.joinable())
        return false;

    cfg_ = cfg;
    gamePort_ = gamePort;
    currentPlayersFn_ = std::move(currentPlayersFn);
    shouldStop_.store(false, std::memory_order_relaxed);

    if (!udpEndpoint_.open(nullptr, 0)) {
        SDL_Log("[discovery] global advertiser UDP punch helper disabled");
    }

    thread_ = std::thread(&GlobalServerAdvertiser::loop, this);
    return true;
}

void GlobalServerAdvertiser::stop()
{
    shouldStop_.store(true, std::memory_order_relaxed);
    if (thread_.joinable())
        thread_.join();
    udpEndpoint_.close();
}

void GlobalServerAdvertiser::loop()
{
    while (!shouldStop_.load(std::memory_order_relaxed)) {
        const auto kind = serverId_.load(std::memory_order_relaxed) == 0
                              ? net::discovery::DirectoryMessage::RegisterServer
                              : net::discovery::DirectoryMessage::Heartbeat;
        if (sendRegistration(kind)) {
            sendUdpHello();
        }
        pollPunchRequests();

        for (int i = 0; i < 20 && !shouldStop_.load(std::memory_order_relaxed); ++i) {
            pollPunchRequests();
            SDL_Delay(100);
        }
    }
}

bool GlobalServerAdvertiser::sendRegistration(net::discovery::DirectoryMessage kind)
{
    NET_StreamSocket* socket = connectDirectory(cfg_, 1200);
    if (!socket)
        return false;

    const int currentPlayers = currentPlayersFn_ ? currentPlayersFn_() : 0;
    const net::discovery::ServerRegistration reg{.serverId = serverId_.load(std::memory_order_relaxed),
                                                 .name = cfg_.serverName,
                                                 .gamePort = gamePort_,
                                                 .currentPlayers =
                                                     static_cast<std::uint8_t>(std::clamp(currentPlayers, 0, 255)),
                                                 .maxPlayers = cfg_.maxPlayers};
    const std::vector<std::uint8_t> payload = net::discovery::encodeRegistration(kind, reg);
    MessageStream stream(socket);
    bool accepted = false;

    if (stream.send(payload.data(), static_cast<Uint32>(payload.size()))) {
        const Uint64 deadline = SDL_GetTicks() + 1200;
        while (SDL_GetTicks() <= deadline && !accepted) {
            const bool ok = stream.poll([&](const void* data, Uint32 len) {
                net::discovery::DirectoryMessage responseKind{};
                const std::uint8_t* responsePayload = nullptr;
                std::size_t responseLen = 0;
                if (!net::discovery::parseEnvelope(data, len, responseKind, responsePayload, responseLen) ||
                    responseKind != net::discovery::DirectoryMessage::RegisterAck)
                {
                    return;
                }
                const auto ack = net::discovery::decodeRegisterAck(responsePayload, responseLen);
                if (ack && ack->accepted) {
                    serverId_.store(ack->serverId, std::memory_order_relaxed);
                    accepted = true;
                }
            });
            if (!ok)
                break;
            if (!accepted)
                SDL_Delay(20);
        }
    }

    NET_DestroyStreamSocket(socket);
    return accepted;
}

void GlobalServerAdvertiser::sendUdpHello()
{
    const std::uint32_t id = serverId_.load(std::memory_order_relaxed);
    if (!udpEndpoint_.isOpen() || id == 0)
        return;

    sendUdp(udpEndpoint_,
            cfg_.directoryHost,
            cfg_.directoryUdpPort,
            net::discovery::encodeUdpHello(
                {.role = net::discovery::UdpRole::Server, .idOrNonce = id, .gamePort = gamePort_}),
            id);
}

void GlobalServerAdvertiser::pollPunchRequests()
{
    if (!udpEndpoint_.isOpen())
        return;

    net::UdpReceivedMessage msg;
    int drained = 0;
    while (drained < 64 && udpEndpoint_.tryReceive(msg)) {
        ++drained;
        if (auto peer = net::discovery::decodeUdpPunchPeer(msg.payload.data(), msg.payload.size())) {
            sendPunchProbes(*peer);
        }
        msg.from.release();
    }
}

void GlobalServerAdvertiser::sendPunchProbes(const net::discovery::UdpPunchPeer& peer)
{
    if (peer.host.empty() || peer.port == 0)
        return;

    const std::vector<std::uint8_t> probe = makePunchProbe(peer.clientNonce);
    for (int i = 0; i < 12 && !shouldStop_.load(std::memory_order_relaxed); ++i) {
        sendUdp(udpEndpoint_, peer.host, peer.port, probe, serverId_.load(std::memory_order_relaxed));
        SDL_Delay(40);
    }
}
