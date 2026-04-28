/// @file Client.cpp
/// @brief Implementation of the TCP client connection and message I/O.

#include "Client.hpp"

#include "network/MatchStatus.hpp"
#include "network/NetKillEvent.hpp"
#include "network/PacketType.hpp"
#include "network/RegistrySerialization.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>

#include <SDL3_net/SDL_net.h>

bool Client::init(const char* addr, Uint16 port)
{
    serverAddr = NET_ResolveHostname(addr);
    if (NET_WaitUntilResolved(serverAddr, -1) == NET_FAILURE) {
        SDL_Log("Failed to resolve server address: %s", SDL_GetError());
        return false;
    }

    auto sock = NET_CreateClient(serverAddr, port);
    if (!sock) {
        SDL_Log("Failed to create client %s", SDL_GetError());
        return false;
    }
    NET_SetStreamSocketNoDelay(sock, true);

    if (NET_WaitUntilConnected(sock, -1) == NET_FAILURE) {
        SDL_Log("Client: connection failed: %s", SDL_GetError());
        NET_DestroyStreamSocket(sock);
        sock = nullptr;
        return false;
    }

    msgStream.socket = sock;

    SDL_Log("Client created, server address is %s", NET_GetAddressString(serverAddr));
    return true;
}

void Client::shutdown()
{
    if (msgStream.socket) {
        NET_DestroyStreamSocket(msgStream.socket);
        msgStream.socket = nullptr;
    }

    if (serverAddr) {
        NET_UnrefAddress(serverAddr);
        serverAddr = nullptr;
    }
}

bool Client::send(const void* data, uint32_t len)
{
    stats.bytesSentTotal += len + 4; // +4 for length prefix
    bytesSentWindow += len + 4;
    return msgStream.send(data, len);
}

void Client::sendPing()
{
    Uint64 now = SDL_GetPerformanceCounter();
    uint8_t buf[1 + sizeof(Uint64)];
    buf[0] = static_cast<uint8_t>(PacketType::PING);
    std::memcpy(buf + 1, &now, sizeof(Uint64));
    send(buf, sizeof(buf));
}

void Client::updateStats(float dt)
{
    statsAccumulator += dt;
    if (statsAccumulator >= 1.0f) {
        stats.recvBytesPerSec = static_cast<float>(bytesRecvWindow) / statsAccumulator;
        stats.sendBytesPerSec = static_cast<float>(bytesSentWindow) / statsAccumulator;
        stats.registryUpdatesPerSec = static_cast<float>(registryUpdatesWindow) / statsAccumulator;
        bytesRecvWindow = 0;
        bytesSentWindow = 0;
        registryUpdatesWindow = 0;
        statsAccumulator = 0.0f;
    }
}

bool Client::sendInputSnapshot(const InputSnapshot& snap)
{
    uint8_t buf[1 + sizeof(InputSnapshot)];
    buf[0] = static_cast<uint8_t>(PacketType::INPUT);
    std::memcpy(buf + 1, &snap, sizeof(InputSnapshot));
    return send(buf, sizeof(buf));
}

bool Client::poll(Registry& registry)
{
    // packet format is 4 byte length prefix
    bool ok = msgStream.poll([&](const void* data, Uint32 size) {
        stats.bytesRecvTotal += size + 4; // +4 for length prefix
        bytesRecvWindow += size + 4;

        if (size < 1)
            return;
        auto type = static_cast<PacketType>(static_cast<const uint8_t*>(data)[0]);
        const uint8_t* payload = static_cast<const uint8_t*>(data) + 1;
        uint32_t payloadSize = size - 1;

        switch (type) {
        case PacketType::ASSIGN_CLIENT_ID:
            if (payloadSize != sizeof(entt::entity)) {
                SDL_Log("Client: received ASSIGN_CLIENT_ID packet of invalid size %u (expected %zu)",
                        payloadSize,
                        sizeof(entt::entity));
                return;
            }
            entt::entity assignedEntity;
            std::memcpy(&assignedEntity, payload, sizeof(entt::entity));
            localPlayerEntity = assignedEntity;
            break;
        case PacketType::UPDATE_REGISTRY:
            if (!registryLoader)
                registryLoader.emplace(registry);
            registryLoader->apply(payload, payloadSize, localPlayerEntity);
            stats.registryUpdateSize = payloadSize;
            ++registryUpdatesWindow;

            // call the local player ready callback once we have the registry update that includes the local player
            if (!localPlayerReadyNotified && localPlayerEntity && localPlayerReadyFn) {
                auto local = registryLoader->map(*localPlayerEntity);
                if (local != entt::null) {
                    localPlayerReadyFn(local);
                    localPlayerReadyNotified = true;
                }
            }

            break;
        case PacketType::PARTICLE_SPAWN: {
            if (payloadSize < sizeof(uint32_t))
                break;
            uint32_t count = 0;
            std::memcpy(&count, payload, sizeof(uint32_t));
            const uint8_t* eventData = payload + sizeof(uint32_t);
            const uint32_t expectedSize = sizeof(uint32_t) + count * sizeof(NetParticleEvent);
            if (payloadSize < expectedSize)
                break;

            if (particleEventFn_ && registryLoader && localPlayerEntity) {
                entt::entity localE = registryLoader->map(*localPlayerEntity);
                for (uint32_t i = 0; i < count; ++i) {
                    NetParticleEvent evt;
                    std::memcpy(&evt, eventData + i * sizeof(NetParticleEvent), sizeof(NetParticleEvent));
                    // Map server entity to client entity
                    evt.source = registryLoader->map(evt.source);
                    particleEventFn_(evt, localE);
                }
            }
            break;
        }
        case PacketType::PONG: {
            if (payloadSize != sizeof(Uint64))
                break;
            Uint64 sendTime;
            std::memcpy(&sendTime, payload, sizeof(Uint64));
            Uint64 now = SDL_GetPerformanceCounter();
            float rtt =
                static_cast<float>(now - sendTime) / static_cast<float>(SDL_GetPerformanceFrequency()) * 1000.0f;
            stats.rttMs = rtt;
            // Exponential moving average (alpha = 0.2)
            if (stats.avgRttMs <= 0.0f)
                stats.avgRttMs = rtt;
            else
                stats.avgRttMs = stats.avgRttMs * 0.8f + rtt * 0.2f;
            break;
        }
        case PacketType::MATCH_STATE: {
            if (payloadSize != sizeof(MatchStatePacket))
                break;
            MatchStatePacket matchState;
            std::memcpy(&matchState, payload, sizeof(MatchStatePacket));
            if (matchStateUpdateFn_)
                matchStateUpdateFn_(matchState);
            break;
        }
        case PacketType::KILL_EVENT: {
            if (payloadSize < sizeof(uint32_t))
                break;

            uint32_t count = 0;
            std::memcpy(&count, payload, sizeof(uint32_t));

            const uint8_t* eventData = payload + sizeof(uint32_t);
            const uint32_t expectedSize = sizeof(uint32_t) + count * sizeof(NetKillEvent);
            if (payloadSize < expectedSize)
                break;

            if (killEventFn_) {
                for (uint32_t i = 0; i < count; ++i) {
                    NetKillEvent evt;
                    std::memcpy(&evt, eventData + i * sizeof(NetKillEvent), sizeof(NetKillEvent));

                    killEventFn_(evt);
                }
            }
        }
        default:
            SDL_Log("Client: unknown message type %d", static_cast<int>(type));
        }
    });

    if (!ok) {
        SDL_Log("Client: server dead");
        NET_DestroyStreamSocket(msgStream.socket);
        return false;
    }

    return ok;
}
