#include "lobby/LobbyManager.hpp"

#include "network/Server.hpp"

#include <algorithm>

bool LobbyManager::init(Server& serverPtr)
{
    server = &serverPtr;
    return true;
}

bool LobbyManager::addPlayer(ClientId id)
{
    SDL_Log("LobbyManager: adding player with clientId %u", id.value);
    LobbyPlayer newPlayer{id};
    LobbyUpdateEvent updateEvent{.type = LobbyUpdateEvent::Type::PlayerJoined, .id = id};

    // If first player to join
    if (players.size() == 0) {
        newPlayer.isHost = true;
        hostId = id;
    }

    players.push_back(newPlayer);
    joinTimes[id] = std::chrono::steady_clock::now();
    server->sendLobbyStateToClient(id, players);
    server->broadcastLobbyUpdate(updateEvent);
    return true;
}

bool LobbyManager::removePlayer(ClientId id)
{
    SDL_Log("LobbyManager: removing player with clientId %u", id.value);
    const auto playerIt =
        std::find_if(players.begin(), players.end(), [id](const LobbyPlayer& p) { return p.id == id; });
    if (playerIt == players.end())
        return false;

    const bool wasHost = playerIt->isHost;
    auto it = std::remove_if(players.begin(), players.end(), [id](const LobbyPlayer& p) { return p.id == id; });
    players.erase(it, players.end());
    joinTimes.erase(id);
    server->broadcastLobbyUpdate(LobbyUpdateEvent{.type = LobbyUpdateEvent::Type::PlayerLeft, .id = id});

    if (wasHost)
        assignNewHost();
    return true;
}

ClientId LobbyManager::assignNewHost()
{
    if (players.empty()) {
        hostId = ClientId{-1};
        return ClientId{0};
    }

    // Assign new host to the player who has been in the lobby the longest
    auto newHostIt =
        std::min_element(players.begin(), players.end(), [this](const LobbyPlayer& a, const LobbyPlayer& b) {
            return joinTimes[a.id] < joinTimes[b.id];
        });

    if (newHostIt == players.end()) {
        SDL_Log("LobbyManager: failed to assign new host, no players found");
        return ClientId{0};
    }

    SDL_Log("LobbyManager: new host assigned with clientId %u", newHostIt->id.value);
    for (auto& p : players)
        p.isHost = false;
    newHostIt->isHost = true;
    hostId = newHostIt->id;
    server->broadcastLobbyUpdate(LobbyUpdateEvent{.type = LobbyUpdateEvent::Type::PlayerNewHost, .id = hostId});
    return hostId;
}
