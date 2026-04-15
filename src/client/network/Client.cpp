/// @file Client.cpp
/// @brief Implementation of the TCP client connection and message I/O.

#include "Client.hpp"

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
    return msgStream.send(data, len);
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
            registryLoader->apply(payload, payloadSize);

            // call the local player ready callback once we have the registry update that includes the local player
            if (!localPlayerReadyNotified && localPlayerEntity && localPlayerReadyFn) {
                auto local = registryLoader->map(*localPlayerEntity);
                if (local != entt::null) {
                    localPlayerReadyFn(local);
                    localPlayerReadyNotified = true;
                }
            }

            break;
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
