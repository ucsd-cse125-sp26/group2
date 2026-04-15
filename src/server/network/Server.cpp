/// @file Server.cpp
/// @brief Implementation of the TCP game server.

#include "Server.hpp"

#include "ecs/components/ClientId.hpp"
#include "ecs/components/InputSnapshot.hpp"
#include "network/PacketType.hpp"
#include "network/RegistrySerialization.hpp"
#include "systems/EventQueue.hpp"
#include "systems/InputReceiveSystem.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>
#include <entt/entity/entity.hpp>

bool Server::init(const char* addr, Uint16 port)
{
    NET_Address* netAddr = NET_ResolveHostname(addr);
    if (NET_WaitUntilResolved(netAddr, -1) == NET_FAILURE) {
        SDL_Log("Server: failed to resolve address: %s", SDL_GetError());
        NET_UnrefAddress(netAddr);
        return false;
    }

    server = NET_CreateServer(netAddr, port);
    NET_UnrefAddress(netAddr);
    if (!server) {
        SDL_Log("Server: failed to create server: %s", SDL_GetError());
        return false;
    }

    eventQueue = EventQueue();
    SDL_Log("Server: listening on port %d", static_cast<int>(port));

    nextClientId.value = 0;
    return true;
}

void Server::shutdown()
{
    if (server) {
        SDL_Log("Server: shutting down");
        NET_DestroyServer(server);
        server = nullptr;
    }
    for (auto& [_, client] : clients) {
        NET_DestroyStreamSocket(client.msgStream.socket);
    }
    clients.clear();
}

bool Server::send(const ClientId& clientId, const void* data, int len)
{
    auto& msgStream = clients.at(clientId).msgStream;

    auto msgLen = static_cast<Uint32>(len);
    NET_WriteToStreamSocket(msgStream.socket, &msgLen, sizeof(msgLen));
    return NET_WriteToStreamSocket(msgStream.socket, data, len);
}

void Server::poll()
{
    acceptClients();
    readClients();
}

void Server::acceptClients()
{
    // Accept up to one new client per tick, should be good enough.

    NET_StreamSocket* socket = nullptr;
    if (!NET_AcceptClient(server, &socket)) {
        SDL_Log("NET_AcceptClient failed: %s", SDL_GetError());
        return;
    } else if (socket) {
        SDL_Log("Server: accepted new client");
        ClientId clientId = getNextClientId();
        clients.insert(
            {clientId,
             Connection{.msgStream = MessageStream(socket), .clientId = clientId, .pendingInitialization = true}});
        eventQueue.enqueue(Event{.clientId = clientId, .type = EventType::Connected, .movementIntent = {}});
    }
}

void Server::readClients()
{
    // packet format is 4 byte length prefix
    for (auto it = clients.begin(); it != clients.end();) {
        auto& conn = it->second;

        bool ok =
            conn.msgStream.poll([this, &conn](const void* data, Uint32 size) { handleMessage(conn, data, size); });

        if (!ok) {
            SDL_Log("Server: client dead");
            NET_DestroyStreamSocket(conn.msgStream.socket);
            it = clients.erase(it);
            continue;
        }

        ++it;
    }
}

void Server::handleMessage(Connection& conn, const void* data, Uint32 len)
{
    if (len < 1)
        return;

    auto type = static_cast<PacketType>(reinterpret_cast<const Uint8*>(data)[0]);
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(data) + 1;
    uint32_t payloadLen = len - 1;

    switch (type) {
    case PacketType::INPUT: {
        if (payloadLen != sizeof(InputSnapshot)) {
            SDL_Log(
                "Server: received INPUT packet of invalid size %u (expected %zu)", payloadLen, sizeof(InputSnapshot));
            return;
        }
        Event event = systems::runInputReceive(payload);
        event.clientId = conn.clientId;
        eventQueue.enqueue(event);
        SDL_Log("Server: received INPUT packet from client %d", event.clientId.value);

        // SDL_Log("Server: received input packet:\n"
        //         "\tforward=%d\n"
        //         "\tback=%d\n"
        //         "\tleft=%d\n"
        //         "\tright=%d\n"
        //         "\tjump=%d\n"
        //         "\tcrouch=%d\n"
        //         "\tyaw=%.2f\n"
        //         "\tpitch=%.2f\n"
        //         "\troll=%.2f",
        //         event.movementIntent.forward,
        //         event.movementIntent.back,
        //         event.movementIntent.left,
        //         event.movementIntent.right,
        //         event.movementIntent.jump,
        //         event.movementIntent.crouch,
        //         event.movementIntent.yaw,
        //         event.movementIntent.pitch,
        //         event.movementIntent.roll);

        break;
    }

    default:
        SDL_Log("Server: received unknown packet type %d", static_cast<int>(type));
        break;
    }
}

ClientId Server::getNextClientId()
{
    ClientId id = nextClientId;
    nextClientId.value++;
    return id;
}

bool Server::isEmpty()
{
    return eventQueue.isEmpty();
}

Event Server::dequeueEvent()
{
    return eventQueue.dequeue();
}

// NOTE: playerEntity is the entity id of the player
bool Server::notifyPlayerClientId(ClientId clientId, entt::entity playerEntity)
{
    auto& conn = clients.at(clientId);
    uint8_t buf[1 + sizeof(entt::entity)];
    buf[0] = static_cast<uint8_t>(PacketType::ASSIGN_CLIENT_ID);
    std::memcpy(buf + 1, &playerEntity, sizeof(entt::entity));
    if (!send(clientId, buf, sizeof(buf))) {
        SDL_Log("Server: failed to send ASSIGN_CLIENT_ID packet to client %d", clientId.value);
        return false;
    }

    conn.pendingInitialization = false;
    return true;
}

void Server::broadcastRegistry(const Registry& registry)
{
    auto buf = registry_serialization::serialize(registry);

    buf.insert(buf.begin(), static_cast<uint8_t>(PacketType::UPDATE_REGISTRY));

    for (const auto& [clientId, conn] : clients) {
        if (!send(clientId, buf.data(), static_cast<int>(buf.size()))) {
            // might need to do more on failure in the future
            SDL_Log("Server: failed to send UPDATE_REGISTRY packet to client %d", clientId.value);
        }
    }
}
