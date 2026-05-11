#include "network/Server.hpp"
#include "network/lobby/LobbyStatus.hpp"

class LobbyManager
{
public:
    bool init(Server& serverPtr);
    bool addPlayer(ClientId id);
    bool removePlayer(ClientId id);

private:
    Server* server = nullptr;
    std::vector<LobbyPlayer> players;
};
