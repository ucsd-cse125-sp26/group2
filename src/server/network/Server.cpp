#include "Server.hpp"

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
        clients.emplace_back();
        clients.back().msgStream.socket = socket;
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
    event.clientId = 0;
    event.moveIntentVector = {1.0f, 1.0f};
    event.jumpIntent = false;
    event.shootIntent = false;

    eventQueue.enqueue(event);
    SDL_Log("Server: event queue size: %d", eventQueue.size());
}

bool Server::isEmpty()
{
    return eventQueue.isEmpty();
}

Event Server::dequeueEvent()
{
    return eventQueue.dequeue();
}
