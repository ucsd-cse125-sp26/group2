#include "lobby/LobbyManager.hpp"

#include "network/Server.hpp"

bool LobbyManager::init(Server& serverPtr)
{
    server = &serverPtr;
    return true;
}

bool LobbyManager::addPlayer(ClientId id)
{
    SDL_Log("LobbyManager: adding player with clientId %u", id.value);
    server->sendLobbyStateToClient(id, players);
    players.push_back(LobbyPlayer{id});
    server->broadcastLobbyUpdate(LobbyUpdateEvent{.type = LobbyUpdateEvent::Type::PlayerJoined, .id = id});
    return true;
}

bool LobbyManager::removePlayer(ClientId id)
{
    SDL_Log("LobbyManager: removing player with clientId %u", id.value);
    auto it = std::remove_if(players.begin(), players.end(), [id](const LobbyPlayer& p) { return p.id == id; });
    if (it != players.end()) {
        players.erase(it, players.end());
        server->broadcastLobbyUpdate(LobbyUpdateEvent{.type = LobbyUpdateEvent::Type::PlayerLeft, .id = id});
        return true;
    }
    return false;
}
