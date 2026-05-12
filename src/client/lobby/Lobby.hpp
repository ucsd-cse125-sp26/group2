#pragma once
#include "IScreen.hpp"
#include "network/Client.hpp"
#include "network/lobby/LobbyStatus.hpp"

#include <optional>
#include <vector>

#ifdef USE_HYBRID_RENDERER
#include "renderer/HybridRenderer.hpp"
using ClientRenderer = HybridRenderer;
#else
#include "renderer/Renderer.hpp"
using ClientRenderer = Renderer;
#endif

class Lobby : public IScreen
{
public:
    bool init(ClientRenderer* rendererPtr, SDL_Window* windowPtr, Client* clientPtr);
    SDL_AppResult event(SDL_Event* event) override;
    SDL_AppResult iterate() override;
    void quit() override;
    bool shouldStartMatch() const;
    std::optional<MatchStatePacket> consumeStartMatchState();

private:
    bool canHostStartMatch() const;
    void updateStartCountdown();

    ClientRenderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    Client* client = nullptr;
    std::vector<LobbyPlayer> players;
    ClientId localClientId{-1};
    std::optional<MatchStatePacket> startMatchState;
    bool startCountdownActive = false;
    float startCountdownRemaining = 0.0f;
    Uint64 lastStartCountdownTickNs = 0;
};
