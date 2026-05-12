#pragma once
#include "IScreen.hpp"
#include "network/Client.hpp"
#include "network/lobby/LobbyStatus.hpp"
#include "renderer-new/NewRenderer.hpp"

#include <optional>
#include <vector>

class Lobby : public IScreen
{
public:
    bool init(NewRenderer* rendererPtr, SDL_Window* windowPtr, Client* clientPtr);
    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;
    bool shouldStartMatch() const;
    std::optional<MatchStatePacket> consumeStartMatchState();

private:
    bool canHostStartMatch() const;
    void updateStartCountdown();

    NewRenderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    Client* client = nullptr;
    std::vector<LobbyPlayer> players;
    ClientId localClientId{-1};
    std::optional<MatchStatePacket> startMatchState;
    bool startCountdownActive = false;
    float startCountdownRemaining = 0.0f;
    Uint64 lastStartCountdownTickNs = 0;
};
