/// @file GlobalDiscoveryClient.cpp
/// @brief Client-side global server browser and NAT-assist helper.

#include "GlobalDiscoveryClient.hpp"

#include "network/MessageStream.hpp"
#include "network/transport/PacketHeader.hpp"
#include "network/transport/UdpEndpoint.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <cstring>

namespace
{
NET_StreamSocket* connectDirectory(const GlobalDiscoveryConfig& cfg, int timeoutMs, std::string& outError)
{
    NET_Address* addr = NET_ResolveHostname(cfg.directoryHost.c_str());
    if (!addr) {
        outError = SDL_GetError();
        return nullptr;
    }

    const NET_Status resolveStatus = NET_WaitUntilResolved(addr, timeoutMs);
    if (resolveStatus != NET_SUCCESS) {
        outError = SDL_GetError();
        NET_UnrefAddress(addr);
        return nullptr;
    }

    NET_StreamSocket* socket = NET_CreateClient(addr, cfg.directoryTcpPort);
    NET_UnrefAddress(addr);
    if (!socket) {
        outError = SDL_GetError();
        return nullptr;
    }

    NET_SetStreamSocketNoDelay(socket, true);
    const NET_Status connectStatus = NET_WaitUntilConnected(socket, timeoutMs);
    if (connectStatus != NET_SUCCESS) {
        outError = SDL_GetError();
        NET_DestroyStreamSocket(socket);
        return nullptr;
    }

    return socket;
}

bool sendAndWait(NET_StreamSocket* socket,
                 const std::vector<std::uint8_t>& request,
                 net::discovery::DirectoryMessage expected,
                 std::vector<std::uint8_t>& responsePayload,
                 std::string& outError,
                 int timeoutMs)
{
    MessageStream stream(socket);
    if (!stream.send(request.data(), static_cast<Uint32>(request.size()))) {
        outError = SDL_GetError();
        return false;
    }

    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeoutMs);
    bool gotResponse = false;
    bool badResponse = false;
    while (SDL_GetTicks() <= deadline && !gotResponse && !badResponse) {
        const bool ok = stream.poll([&](const void* data, Uint32 len) {
            net::discovery::DirectoryMessage kind{};
            const std::uint8_t* payload = nullptr;
            std::size_t payloadLen = 0;
            if (!net::discovery::parseEnvelope(data, len, kind, payload, payloadLen) || kind != expected) {
                badResponse = true;
                return;
            }
            responsePayload.assign(payload, payload + payloadLen);
            gotResponse = true;
        });
        if (!ok) {
            outError = SDL_GetError();
            return false;
        }
        if (!gotResponse)
            SDL_Delay(10);
    }

    if (badResponse) {
        outError = "directory returned an unexpected response";
        return false;
    }
    if (!gotResponse) {
        outError = "directory request timed out";
        return false;
    }
    return true;
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

    net::UdpEndpointAddr dest{.addr = addr, .port = port};
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

bool GlobalDiscoveryClient::fetchServers(const GlobalDiscoveryConfig& cfg,
                                         std::vector<net::discovery::ServerInfo>& outServers,
                                         std::string& outError,
                                         int timeoutMs)
{
    outServers.clear();
    outError.clear();
    if (!cfg.enabled)
        return true;

    NET_StreamSocket* socket = connectDirectory(cfg, timeoutMs, outError);
    if (!socket)
        return false;

    std::vector<std::uint8_t> response;
    const bool ok = sendAndWait(socket,
                                net::discovery::makeEnvelope(net::discovery::DirectoryMessage::ListRequest),
                                net::discovery::DirectoryMessage::ListResponse,
                                response,
                                outError,
                                timeoutMs);
    NET_DestroyStreamSocket(socket);
    if (!ok)
        return false;

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
    outError.clear();
    if (!cfg.enabled || serverId == 0 || timeoutMs <= 0)
        return false;

    const std::uint32_t nonce = net::discovery::randomNonce();
    net::UdpEndpoint udp;
    if (udp.open(nullptr, 0)) {
        sendUdp(udp,
                cfg.directoryHost,
                cfg.directoryUdpPort,
                net::discovery::encodeUdpHello(
                    {.role = net::discovery::UdpRole::Client, .idOrNonce = nonce, .gamePort = 0}),
                nonce);
    }

    NET_StreamSocket* socket = connectDirectory(cfg, timeoutMs, outError);
    if (!socket)
        return false;

    std::vector<std::uint8_t> response;
    const bool ok = sendAndWait(socket,
                                net::discovery::encodePunchRequest({.serverId = serverId, .clientNonce = nonce}),
                                net::discovery::DirectoryMessage::PunchResponse,
                                response,
                                outError,
                                timeoutMs);
    NET_DestroyStreamSocket(socket);
    if (!ok)
        return false;

    const auto decoded = net::discovery::decodePunchResponse(response.data(), response.size());
    if (!decoded || !decoded->accepted) {
        outError = decoded ? decoded->message : "directory returned a malformed punch response";
        return false;
    }

    outServer = decoded->server;

    if (udp.isOpen() && !outServer.udpHost.empty() && outServer.udpPort != 0) {
        const std::vector<std::uint8_t> probe = makePunchProbe(nonce);
        const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeoutMs);
        while (SDL_GetTicks() <= deadline) {
            sendUdp(udp, outServer.udpHost, outServer.udpPort, probe, nonce);
            net::UdpReceivedMessage msg;
            while (udp.tryReceive(msg)) {
                msg.from.release();
            }
            SDL_Delay(50);
        }
    }

    return true;
}
