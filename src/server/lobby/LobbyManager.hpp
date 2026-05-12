#pragma once

#include "network/Server.hpp"
#include "network/lobby/LobbyStatus.hpp"

#include <chrono>
#include <unordered_map>

class LobbyManager
{
public:
    bool init(Server& serverPtr);
    bool addPlayer(ClientId id);
    bool removePlayer(ClientId id);
    bool setPlayerReadyStatus(ClientId id, bool ready);
    bool hostStartMatch(ClientId sender);
    void resetReadyStatuses();

private:
    Server* server = nullptr;
    std::vector<LobbyPlayer> players;
    std::unordered_map<ClientId, std::chrono::steady_clock::time_point> joinTimes;
    ClientId hostId{-1};

    ClientId assignNewHost();
    void sendLobbyStateToAllPlayers();
};
