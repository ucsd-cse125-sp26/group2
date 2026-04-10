#include "Server.hpp"

#include "network/InputPacket.hpp"
#include "systems/EventQueue.hpp"

#include <SDL3/SDL.h>

#include <SDL3_net/SDL_net.h>

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

    nextClientId = 0;
    return true;
}

void Server::shutdown()
{
    if (server) {
        SDL_Log("Server: shutting down");
        NET_DestroyServer(server);
        server = nullptr;
    }
    for (auto& client : clients) {
        NET_DestroyStreamSocket(client.msgStream.socket);
    }
    clients.clear();
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
        auto clientId = nextClientId++;
        clients.emplace_back();
        clients.back().msgStream.socket = socket;
        clients.back().clientId = clientId;
    }
}

void Server::readClients()
{
    // packet format is 4 byte length prefix
    for (auto it = clients.begin(); it != clients.end();) {
        auto& conn = *it;

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

    SDL_Log("Server: received %d bytes", len);
    SDL_Log("Server: data: %.*s", len, static_cast<const char*>(data));

    // echo
    conn.msgStream.send(data, len);

    // temp enqueue of events
    Event event;
    event.clientId = conn.clientId;
    event.movementIntent.forward = true;

    eventQueue.enqueue(event);
    conn.msgStream.send("Message received", 16);

    SDL_Log("Server: received input packet:\n"
            "\tforward=%d\n"
            "\tback=%d\n"
            "\tleft=%d\n"
            "\tright=%d\n"
            "\tjump=%d\n"
            "\tcrouch=%d\n"
            "\tyaw=%.2f\n"
            "\tpitch=%.2f\n"
            "\troll=%.2f",
            event.movementIntent.forward,
            event.movementIntent.back,
            event.movementIntent.left,
            event.movementIntent.right,
            event.movementIntent.jump,
            event.movementIntent.crouch,
            event.movementIntent.yaw,
            event.movementIntent.pitch,
            event.movementIntent.roll);
}

bool Server::isEmpty()
{
    return eventQueue.isEmpty();
}

Event Server::dequeueEvent()
{
    return eventQueue.dequeue();
}

Event Server::deserializePacket(const InputPacket& pkt)
{
    Event event;

    event.movementIntent.forward = pkt.forward;
    event.movementIntent.back = pkt.back;
    event.movementIntent.left = pkt.left;
    event.movementIntent.right = pkt.right;
    event.movementIntent.jump = pkt.jump;
    event.movementIntent.crouch = pkt.crouch;
    event.movementIntent.yaw = pkt.yaw;
    event.movementIntent.pitch = pkt.pitch;
    event.movementIntent.roll = pkt.roll;

    return event;
}
