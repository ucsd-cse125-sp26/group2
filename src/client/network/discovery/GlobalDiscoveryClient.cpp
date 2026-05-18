/// @file GlobalDiscoveryClient.cpp
/// @brief Client-side UDP global server browser and NAT-assist helper.

#include "GlobalDiscoveryClient.hpp"

#include "network/transport/PacketHeader.hpp"
#include "network/transport/UdpEndpoint.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

namespace
{
bool resolveDirectory(const GlobalDiscoveryConfig& cfg, net::UdpEndpointAddr& out, std::string& outError, int timeoutMs)
{
    NET_Address* addr = NET_ResolveHostname(cfg.directoryHost.c_str());
    if (!addr) {
        outError = SDL_GetError();
        return false;
    }
    if (NET_WaitUntilResolved(addr, timeoutMs) != NET_SUCCESS) {
        outError = SDL_GetError();
        NET_UnrefAddress(addr);
        return false;
    }
    out.release();
    out.addr = addr;
    out.port = cfg.directoryUdpPort;
    return true;
}

bool sendDirectory(net::UdpEndpoint& udp, const net::UdpEndpointAddr& dest, const std::vector<std::uint8_t>& payload)
{
    net::PacketHeader hdr{};
    hdr.kind = static_cast<std::uint8_t>(net::PacketKind::DirectoryControl);
    hdr.channel = static_cast<std::uint8_t>(net::ChannelId::ControlReliableOrdered);
    return udp.send(dest, hdr, payload.data(), static_cast<int>(payload.size()));
}

bool requestDirectory(const GlobalDiscoveryConfig& cfg,
                      const std::vector<std::uint8_t>& request,
                      net::discovery::DirectoryMessage expected,
                      std::vector<std::uint8_t>& responsePayload,
                      std::string& outError,
                      int timeoutMs)
{
    net::UdpEndpoint udp;
    if (!udp.open(nullptr, 0)) {
        outError = SDL_GetError();
        return false;
    }

    net::UdpEndpointAddr dir;
    if (!resolveDirectory(cfg, dir, outError, timeoutMs))
        return false;

    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeoutMs);
    Uint64 lastSend = 0;
    while (SDL_GetTicks() <= deadline) {
        const Uint64 now = SDL_GetTicks();
        if (lastSend == 0 || now - lastSend >= 100) {
            sendDirectory(udp, dir, request);
            lastSend = now;
        }

        net::UdpReceivedMessage msg;
        while (udp.tryReceive(msg)) {
            if (msg.header.kind == static_cast<std::uint8_t>(net::PacketKind::DirectoryControl)) {
                net::discovery::DirectoryMessage kind{};
                const std::uint8_t* payload = nullptr;
                std::size_t payloadLen = 0;
                if (net::discovery::parseEnvelope(msg.payload.data(), msg.payload.size(), kind, payload, payloadLen) &&
                    kind == expected)
                {
                    responsePayload.assign(payload, payload + payloadLen);
                    msg.from.release();
                    return true;
                }
            }
            msg.from.release();
        }
        SDL_Delay(10);
    }

    outError = "directory request timed out";
    return false;
}
} // namespace

bool GlobalDiscoveryClient::fetchServers(const GlobalDiscoveryConfig& cfg,
                                         std::vector<net::discovery::ServerInfo>& outServers,
                                         std::string& outError,
                                         int timeoutMs)
{
    outServers.clear();
    outError.clear();
    if (!cfg.enabled)
        return true;

    std::vector<std::uint8_t> response;
    if (!requestDirectory(cfg,
                          net::discovery::makeEnvelope(net::discovery::DirectoryMessage::ListRequest),
                          net::discovery::DirectoryMessage::ListResponse,
                          response,
                          outError,
                          timeoutMs))
    {
        return false;
    }

    const auto decoded = net::discovery::decodeServerList(response.data(), response.size());
    if (!decoded) {
        outError = "directory returned a malformed server list";
        return false;
    }

    outServers = *decoded;
    return true;
}

bool GlobalDiscoveryClient::requestHolePunch(const GlobalDiscoveryConfig& cfg,
                                             std::uint32_t serverId,
                                             net::discovery::ServerInfo& outServer,
                                             std::string& outError,
                                             int timeoutMs)
{
    return requestHolePunch(cfg, serverId, net::discovery::randomNonce(), outServer, outError, timeoutMs);
}

bool GlobalDiscoveryClient::requestHolePunch(const GlobalDiscoveryConfig& cfg,
                                             std::uint32_t serverId,
                                             std::uint32_t clientNonce,
                                             net::discovery::ServerInfo& outServer,
                                             std::string& outError,
                                             int timeoutMs,
                                             std::uint64_t* outRelayToken)
{
    outError.clear();
    if (!cfg.enabled || serverId == 0 || timeoutMs <= 0)
        return false;

    std::vector<std::uint8_t> response;
    if (!requestDirectory(cfg,
                          net::discovery::encodePunchRequest({.serverId = serverId, .clientNonce = clientNonce}),
                          net::discovery::DirectoryMessage::PunchResponse,
                          response,
                          outError,
                          timeoutMs))
    {
        return false;
    }

    const auto decoded = net::discovery::decodePunchResponse(response.data(), response.size());
    if (!decoded || !decoded->accepted) {
        outError = decoded ? decoded->message : "directory returned a malformed punch response";
        return false;
    }

    outServer = decoded->server;
    if (outRelayToken)
        *outRelayToken = decoded->relayToken;
    return true;
}
