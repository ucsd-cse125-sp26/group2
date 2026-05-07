#pragma once
#include "IScreen.hpp"
#include "network/Client.hpp"
#include "network/lobby/LobbyStatus.hpp"

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

private:
    ClientRenderer* renderer = nullptr;
    SDL_Window* window = nullptr;
    Client* client = nullptr;
    std::vector<LobbyPlayer> players;
};
